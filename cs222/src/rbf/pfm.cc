#include "pfm.h"
#include "returncodes.h"

#include <sys/stat.h>
#include <assert.h>
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
    FILE* file = fopen(fileName, "rb");
    if (file)
    {
        fclose(file);
        return rc::FILE_ALREADY_EXISTS;
    }

    // Open for binary write - to create the file
    file = fopen(fileName, "wb");
    if (file)
    {
        fclose(file);
    }

    return rc::OK;
}


RC PagedFileManager::destroyFile(const char *fileName)
{
    if (remove(fileName) != 0)
    {
        return rc::FILE_COULD_NOT_DELETE;
    }

    return rc::OK;
}


RC PagedFileManager::openFile(const char *fileName, FileHandle &fileHandle)
{
    if (fileHandle.hasFile())
    {
        return rc::FILE_HANDLE_ALREADY_INITIALIZED;
    }

    // Open file read only (will not create) to see if it exists
    FILE* file = fopen(fileName, "rb");
    if (!file)
    {
        return rc::FILE_NOT_FOUND;
    }

    // Open file for reading/writing
    fclose(file);
    file = fopen(fileName, "rb+");
    if (!file)
    {
        return rc::FILE_COULD_NOT_OPEN;
    }

    // Initialize the FileHandle
    fileHandle.loadFile(fileName, file);

    return rc::OK;
}


RC PagedFileManager::closeFile(FileHandle &fileHandle)
{
    if (!fileHandle.hasFile())
    {
        return rc::FILE_HANDLE_NOT_INITIALIZED;
    }

    fileHandle.flushPages();
    fileHandle.unload();

    return rc::OK;
}


FileHandle::FileHandle()
    : _file(NULL)
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

RC FileHandle::loadFile(const char *fileName, FILE* file)
{
    _file = file;

    if (fseek(_file, 0, SEEK_END) != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }
    _numPages = ftell(_file) / PAGE_SIZE;

    return rc::OK;
}

RC FileHandle::unload()
{
    // Prepare handle for reuse
    if (_file)
    {
        fclose(_file);
    }
    _file = NULL;

    return rc::OK;
}

RC FileHandle::readPage(PageNum pageNum, void *data)
{
    if (pageNum <= _numPages)
    {
        // Read the data from disk into the user buffer
        int result = fseek(_file, PAGE_SIZE * pageNum, SEEK_SET);
        if (result != 0)
        {
            return rc::FILE_SEEK_FAILED;
        }
        size_t read = fread(data, PAGE_SIZE, 1, _file);
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
    if (pageNum <= _numPages)
    {
        // Flush the content in the user buffer to disk and update the page entry
        int result = fseek(_file, PAGE_SIZE * pageNum, SEEK_SET);
        if (result != 0)
        {
            return rc::FILE_SEEK_FAILED;
        }
        size_t written = fwrite(data, PAGE_SIZE, 1, _file);
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
    // Seek to the end of the file (last page) and write the new page data
    int result = fseek(_file, _numPages * PAGE_SIZE, SEEK_SET);
    if (result != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }
    size_t written = fwrite(data, PAGE_SIZE, 1, _file);
    if (written != 1)
    {
        return rc::FILE_CORRUPT;
    }

    // Update our copy of the page count - after committing to disk
    _numPages++;

    return rc::OK;
}


unsigned FileHandle::getNumberOfPages()
{
    return _numPages;
}

