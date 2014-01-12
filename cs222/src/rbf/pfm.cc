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

    // TODO: Check fileHandle hasn't alreaedy been initialized
    // TODO: Initialize fileHandle with file
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


RC PagedFileManager::CloseFile(FileHandle &fileHandle)
{
    // TODO: Check fileHandle is properly initialized
    // TODO: Write out data
    // TODO: Uninitialize fileHandle
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


FileHandle::FileHandle()
{
}


FileHandle::~FileHandle()
{
}


RC FileHandle::ReadPage(PageNum pageNum, void *data)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


RC FileHandle::WritePage(PageNum pageNum, const void *data)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


RC FileHandle::AppendPage(const void *data)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


unsigned FileHandle::GetNumberOfPages()
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


