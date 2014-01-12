#include "rbfm.h"
#include "returncodes.h"

RecordBasedFileManager* RecordBasedFileManager::_rbf_manager = 0;

RecordBasedFileManager* RecordBasedFileManager::Instance()
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

RC RecordBasedFileManager::insertTuple(const string &fileName, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::readTuple(const string &fileName, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::printTuple(const vector<Attribute> &recordDescriptor, const void *data) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}
