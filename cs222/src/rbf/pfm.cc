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

    return rc::OK;
}


FileHandle::FileHandle()
    : _file(NULL)
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


