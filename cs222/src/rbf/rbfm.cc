#include "returncodes.h"
#include "rbfm.h"

RecordBasedFileManager* RecordBasedFileManager::_rbf_manager = 0;

RecordBasedFileManager* RecordBasedFileManager::instance()
{
    if(!_rbf_manager)
        _rbf_manager = new RecordBasedFileManager();

    return _rbf_manager;
}

RecordBasedFileManager::RecordBasedFileManager()
{
}

RecordBasedFileManager::~RecordBasedFileManager()
{
}

RC RecordBasedFileManager::createFile(const string &fileName) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::destroyFile(const string &fileName) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::openFile(const string &fileName, FileHandle &fileHandle) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::closeFile(FileHandle &fileHandle) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::insertRecord(const FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::readRecord(const FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

