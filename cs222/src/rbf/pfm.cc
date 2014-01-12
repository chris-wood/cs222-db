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
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


RC PagedFileManager::DestroyFile(const char *fileName)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


RC PagedFileManager::OpenFile(const char *fileName, FileHandle &fileHandle)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}


RC PagedFileManager::CloseFile(FileHandle &fileHandle)
{
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


