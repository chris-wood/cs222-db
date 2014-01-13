#include "pfm.h"
#include "returncodes.h"

#include <cstring>
#include <cstdlib>

PagedFileManager* PagedFileManager::_pf_manager = 0;


PagedFileManager* PagedFileManager::Instance()
{
    if(!_pf_manager)
        _pf_manager = new PagedFileManager();

    return _pf_manager;
}


PagedFileManager::PagedFileManager()
{
}


PagedFileManager::~PagedFileManager()
{
}


RC PagedFileManager::CreateFile(const char *fileName)
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


RC PagedFileManager::DestroyFile(const char *fileName)
{
    if (remove(fileName) != 0)
    {
        return rc::FILE_COULD_NOT_DELETE;
    }

    // TODO: Keep track that we have deleted this file
    return rc::OK;
}


RC PagedFileManager::OpenFile(const char *fileName, FileHandle &fileHandle)
{
    if (fileHandle.HasFile())
    {
        return rc::FILE_HANDLE_ALREADY_INITIALIZED;
    }

    FILE* file = fopen(fileName, "r");
    if (!file)
    {
        return rc::FILE_NOT_FOUND;
    }

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
    fileHandle.LoadFile(file, header);

    return rc::OK;
}


RC PagedFileManager::CloseFile(FileHandle &fileHandle)
{
    if (!fileHandle.HasFile())
    {
        return rc::FILE_HANDLE_NOT_INITIALIZED;
    }

    // TODO: Write out any changes to the header
    // TODO: Write out data
    int result = fseek(fileHandle.GetFile(), 0, 0);
    if (result != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }
    fwrite(fileHandle.GetHeader(), sizeof(PFHeader), 1, fileHandle.GetFile());

    // TODO: destroy the file handle, and deallocate the header information and directory

    fclose(fileHandle.GetFile());
    fileHandle.SetFile(NULL);

    return rc::OK;
}


FileHandle::FileHandle()
    : _file(NULL), _header(NULL)
{
}


FileHandle::~FileHandle()
{
}

RC FileHandle::LoadFile(FILE* file, PFHeader* header)
{
    int bytesRead;
    int result;
    _file = file;

    // If there are pages in the file, read in the directory information
    _header = header;
    if (_header->numPages > 0)
    {
        // Read in the first directory entry
        PFEntry* entry = (PFEntry*)malloc(sizeof(PFEntry));
        bytesRead = fread((void*)entry, sizeof(PFEntry), 1, _file);
        _directory.push_back(entry);

        // Continue reading more entries, if indicated to do so by the directory entry
        while (entry->nextEntryAbsoluteOffset != -1)
        {
            result = fseek(_file, entry->nextEntryAbsoluteOffset, sizeof(PFHeader));
            if (result != 0)
            {
                return rc::FILE_SEEK_FAILED;
            }
            entry = (PFEntry*)malloc(sizeof(PFEntry));
            bytesRead = fread((void*)entry, sizeof(PFEntry), 1, _file);
            _directory.push_back(entry);
        }
    }

    // Integrity constraint - check to make sure that what's in the header
    // is what we pulled from disk
    if (_header->numPages != _directory.size()) 
    {
        return rc::FILE_HEADER_CORRUPT;
    }
    else
    {
        return rc::OK;
    }
}

RC FileHandle::ReadPage(PageNum pageNum, void *data)
{
    int result = 0;
    int bytesRead = 0;

    for (vector<PFEntry*>::const_iterator itr = _directory.begin(); itr != _directory.end(); itr++)
    {
        if ((*itr)->pageNum == pageNum)
        {
            PFEntry* entry = *itr;

            // Read the data from disk into the user buffer
            result = fseek(_file, entry->nextEntryAbsoluteOffset, 0);
            if (result != 0)
            {
                return rc::FILE_SEEK_FAILED;
            }
            bytesRead = fread(data, PAGE_SIZE, 1, _file);

            return rc::OK;
        }
    }

    return rc::FILE_PAGE_NOT_FOUND;
}


RC FileHandle::WritePage(PageNum pageNum, const void *data)
{
    int result = 0;
    int bytesRead = 0;

    for (vector<PFEntry*>::const_iterator itr = _directory.begin(); itr != _directory.end(); itr++)
    {
        if ((*itr)->pageNum == pageNum)
        {
            PFEntry* entry = *itr;

            // If not in memory, seek to the offset for this page and load it into memory
            if (entry->loaded == false)
            {
                result = fseek(_file, entry->nextEntryAbsoluteOffset, 0);
                if (result != 0)
                {
                    return rc::FILE_SEEK_FAILED;
                }
                // entry->data = (void*)malloc(PAGE_SIZE);
                bytesRead = fread(entry->data, PAGE_SIZE, 1, _file);
                entry->loaded = true;
            }

            // TODO
            // memcpy(entry->data, data, PAGE_SIZE);
            fseek(_file, entry->nextEntryAbsoluteOffset + sizeof(PFEntry), 0);
            fwrite(data, PAGE_SIZE, 1, _file);
            // TODO: write to the file here

            // free(entry->data);
            return rc::OK;
        }
    }

    return rc::FILE_PAGE_NOT_FOUND;
}


RC FileHandle::AppendPage(const void *data)
{
    // Determine the offset for this directory entry
    int offset = _directory.size() * (sizeof(PFEntry) + PAGE_SIZE);

    // Create the new page entry, copy over the data, and add it to the directory
    PFEntry* entry = (PFEntry*)malloc(sizeof(PFEntry));
    entry->pageNum = _directory.size() + 1;
    entry->loaded = false;
    entry->nextEntryAbsoluteOffset = -1;
    entry->offset = offset + sizeof(PFEntry);

    // Update the number of pages
    _header->numPages++;

    // TODO: write to the file here
    // entry->data = (void*)malloc(PAGE_SIZE);
    fseek(_file, offset + sizeof(PFEntry), 0);
    fwrite(data, PAGE_SIZE, 1, _file);
    // memcpy(entry->data, data, PAGE_SIZE);
    // size_t fwrite ( const void * ptr, size_t size, size_t count, FILE * stream );

    // Update the existing directory collection if necessary
    if (_directory.size() > 0)
    {
        _directory.at(_directory.size() - 1)->nextEntryAbsoluteOffset = offset;
    }
    _directory.push_back(entry);

    // OK
    return rc::OK;
}


unsigned FileHandle::GetNumberOfPages()
{
    return _directory.size();
}


