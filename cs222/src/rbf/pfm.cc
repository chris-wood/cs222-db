#include "pfm.h"
#include "returncodes.h"

#include <cstring>
#include <cstdlib>

PagedFileManager* PagedFileManager::_pf_manager = 0;


PagedFileManager* PagedFileManager::instance()
{
    if(!_pf_manager)
    {
        _pf_manager = new PagedFileManager();
    }

    return _pf_manager;
}


PagedFileManager::PagedFileManager()
{
}


PagedFileManager::~PagedFileManager()
{
}


RC PagedFileManager::createFile(const char *fileName)
{
    // Check if the file exists
    FILE* file = fopen(fileName, "r");
    if (file)
    {
        fclose(file);
        return rc::FILE_ALREADY_EXISTS;
    }

    file = fopen(fileName, "w");

    PFHeader header;
    header.headerSize = sizeof(header);
    header.version = CURRENT_PF_VERSION;
    header.numPages = 0;

    // Write out header to file
    size_t written = fwrite(&header, header.headerSize, 1, file);
    fclose(file);

    if (written != 1)
    {
        return rc::UNKNOWN_FAILURE;
    }

    // TODO: Keep track that we have created this file

    return rc::OK;
}


RC PagedFileManager::destroyFile(const char *fileName)
{
    if (remove(fileName) != 0)
    {
        return rc::FILE_COULD_NOT_DELETE;
    }

    // TODO: Keep track of open file handles, so we can force close them
    // TODO: Keep track that we have deleted this file
    return rc::OK;
}


RC PagedFileManager::openFile(const char *fileName, FileHandle &fileHandle)
{
    if (fileHandle.hasFile())
    {
        return rc::FILE_HANDLE_ALREADY_INITIALIZED;
    }

    // Open file read only (will not create) to see if it exists
    FILE* file = fopen(fileName, "r");
    if (!file)
    {
        return rc::FILE_NOT_FOUND;
    }

    // Open file for reading/writing
    fclose(file);
    file = fopen(fileName, "r+");
    if (!file)
    {
        return rc::FILE_COULD_NOT_OPEN;
    }

    // Read in header data
    PFHeader* header = (PFHeader*)malloc(sizeof(PFHeader));
    size_t read = fread(header, sizeof(PFHeader), 1, file);
    if (read != 1 || header->headerSize != sizeof(PFHeader))
    {
        return rc::FILE_CORRUPT;
    }

    if (header->version != CURRENT_PF_VERSION)
    {
        return rc::FILE_OUT_OF_DATE;
    }

    // Initialize the FileHandle
    fileHandle.loadFile(file, header);

    return rc::OK;
}


RC PagedFileManager::closeFile(FileHandle &fileHandle)
{
    if (!fileHandle.hasFile())
    {
        return rc::FILE_HANDLE_NOT_INITIALIZED;
    }

    return rc::OK;
}


FileHandle::FileHandle()
    : _file(NULL), _header(NULL)
{
}


FileHandle::~FileHandle()
{
    unload();
}

RC FileHandle::flushPages()
{
    // Nothing right now because all pages are written out immediately
    return rc::OK;
}

RC FileHandle::unload()
{
    if (_header)
    {
        free(_header);
    }

    fclose(_file);

    // Prepare handle for reuse
    _header = NULL;
    _file = NULL;

    return rc::OK;
}

RC FileHandle::readPage(PageNum pageNum, void *data)
{
    int result;
    size_t read;

    if (pageNum <= _header->numPages)
    {
        // Read the data from disk into the user buffer
        // QUESTION: Can we assume fseek is considered constant access time and not linear?
        result = fseek(_file, PAGE_SIZE * (pageNum + 1), SEEK_SET);
        if (result != 0)
        {
            return rc::FILE_SEEK_FAILED;
        }
        read = fread(data, PAGE_SIZE, 1, _file);
        if (read != 1)
        {
            return rc::FILE_CORRUPT;
        }

        return rc::OK;
    }
    else
    {
        return rc::FILE_PAGE_NOT_FOUND;
    }
}


RC FileHandle::writePage(PageNum pageNum, const void *data)
{
    int result;
    size_t written;

    if (pageNum <= _header->numPages)
    {
        // Flush the content in the user buffer to disk and update the page entry
        result = fseek(_file, PAGE_SIZE * (1 + pageNum), SEEK_SET);
        if (result != 0)
        {
            return rc::FILE_SEEK_FAILED;
        }
        written = fwrite(data, PAGE_SIZE, 1, _file);
        if (written != 1)
        {
            return rc::FILE_CORRUPT;
        }

        return rc::OK;
    }
    else
    {
        return rc::FILE_PAGE_NOT_FOUND;
    }
}


RC FileHandle::appendPage(const void *data)
{
    // Determine the offset for this directory entry and
    // update the number of pages in the process
    int offset = ++(_header->numPages);

    // Update the header information for the file with the new count
    int result = fseek(_file, 0, SEEK_SET);
    if (result != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }
    int written = fwrite(_header, sizeof(PFHeader), 1, _file);
    if (written != 1)
    {
        return rc::FILE_CORRUPT;
    }

    // Now write the new data
    result = fseek(_file, offset * PAGE_SIZE, SEEK_SET);
    if (result != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }
    written = fwrite(data, PAGE_SIZE, 1, _file);
    if (written != 1)
    {
        return rc::FILE_CORRUPT;
    }

    return rc::OK;
}


unsigned FileHandle::getNumberOfPages()
{
    return _header->numPages;
}
