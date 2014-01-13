#include "pfm.h"
#include "returncodes.h"


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
        return rc::FILE_ALREADY_EXISTS;
    }

    fclose(file);
    // NOTE: Possible concurrency issue, file may be created within this window
    file = fopen(fileName, "w");

    // TODO: Write any header data for the file
    // TODO: Keep track that we have created this file

    fclose(file);
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

    if (fileHandle.HasFile())
    {
        return rc::FILE_HANDLE_ALREADY_INITIALIZED;
    }

    fileHandle.SetFile(file);
    return rc::OK;
}


RC PagedFileManager::CloseFile(FileHandle &fileHandle)
{
    if (!fileHandle.HasFile())
    {
        return rc::FILE_HANDLE_NOT_INITIALIZED;
    }

    // TODO: Write out data

    fclose(fileHandle.GetFile());
    fileHandle.SetFile(NULL);

    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


FileHandle::FileHandle()
    : _file(NULL)
{   
}


FileHandle::~FileHandle()
{
}

RC FileHandle::SetFile(FILE* file)
{
    int bytesRead;
    int result;
    _file = file;

    // Read in the first directory entry
    PageEntry* entry = (PageEntry*)malloc(sizeof(PageEntry));
    bytesRead = fread((void*)entry, sizeof(PageEntry), 1, _file);
    _directory.push_back(entry);

    // Continue reading more entries, if indicated to do so by the directory entry
    while (entry->nextEntryAbsoluteOffset != -1)
    {
        result = fseek(_file, entry->nextEntryAbsoluteOffset, 0);
        if (result != 0)
        {
            return rc::FILE_SEEK_FAILED;
        }
        entry = (PageEntry*)malloc(sizeof(PageEntry));
        bytesRead = fread((void*)entry, sizeof(PageEntry), 1, _file);
        _directory.push_back(entry);
    }
}

RC FileHandle::ReadPage(PageNum pageNum, void *data)
{
    int result = 0;
    int bytesRead = 0;

    for (vector<PageEntry*>::const_iterator itr = _directory.begin(); itr != _directory.end(); itr++)
    {
        if ((*itr)->pageNum == pageNum)
        {
            PageEntry* entry = *itr;

            // If not in memory, seek to the offset for this page and load it into memory
            if (entry->loaded == false)
            {
                result = fseek(_file, entry->nextEntryAbsoluteOffset, 0);
                if (result != 0)
                {
                    return rc::FILE_SEEK_FAILED;
                }
                bytesRead = fread(entry->data, PAGE_SIZE, 1, _file);
                entry->loaded = true;
            }

            // Copy over the data and return OK
            memcpy(data, entry->data, PAGE_SIZE);
            return rc::OK;
        }
    }

    return rc::FILE_PAGE_NOT_FOUND;
}


RC FileHandle::WritePage(PageNum pageNum, const void *data)
{
    int result = 0;
    int bytesRead = 0;

    for (vector<PageEntry*>::const_iterator itr = _directory.begin(); itr != _directory.end(); itr++)
    {
        if ((*itr)->pageNum == pageNum)
        {
            PageEntry* entry = *itr;

            // If not in memory, seek to the offset for this page and load it into memory
            if (entry->loaded == false)
            {
                result = fseek(_file, entry->nextEntryAbsoluteOffset, 0);
                if (result != 0)
                {
                    return rc::FILE_SEEK_FAILED;
                }
                bytesRead = fread(entry->data, PAGE_SIZE, 1, _file);
                entry->loaded = true;
            }

            // Re-allocate some space and then copy over the data and return OK
            // caw: need to check that this was actually malloc'd to begin with
            free(entry->data);
            entry->data = (void*)malloc(PAGE_SIZE);


            memcpy(entry->data, data, PAGE_SIZE);
            return rc::OK;
        }
    }

    return rc::FILE_PAGE_NOT_FOUND;
}

// file stream format: entry header, data, entry header, data, entry header, data, ...
RC FileHandle::AppendPage(const void *data)
{
    int result = 0;
    int bytesRead = 0;

    // Add a new page to the directory
    int offset = _directory.size() * (sizeof(PageEntry) + PAGE_SIZE);
    _directory.at(_directory.size() - 1)->nextEntryAbsoluteOffset = offset;

    // Create the new page entry, copy over the data, and add it to the directory
    PageEntry* entry = (PageEntry*)malloc(sizeof(PageEntry));
    entry->data = (void*)malloc(PAGE_SIZE);
    memcpy(entry->data, data, PAGE_SIZE);
    entry->pageNum = _directory.size() + 1;
    // free?
    entry->loaded = true;
    entry->nextEntryAbsoluteOffset = -1;
    entry->offset = offset + sizeof(PageEntry);
    _directory.push_back(entry);
}


unsigned FileHandle::GetNumberOfPages()
{
    return _directory.size();
}


