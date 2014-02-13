#include "ix.h"
#include "../rbf/pfm.h"
#include "../rbf/rbfm.h"
#include "../util/returncodes.h"

IndexManager* IndexManager::_index_manager = 0;

IndexManager* IndexManager::instance()
{
    if(!_index_manager)
        _index_manager = new IndexManager();

    return _index_manager;
}

IndexManager::IndexManager()
{
	// Index Header
	Attribute attr;
	attr.name = "isLeafPage";				attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "firstRecord_PageNum";		attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "firstRecord_SlotNum";		attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "parent_PageNum";			attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "prev_PageNum";				attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "next_PageNum";				attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);

	// Index Non-Leaf Record
	attr.name = "pagePointer_PageNum";		attr.type = TypeInt;		attr.length = 4;			_indexNonLeafRecordDescriptor.push_back(attr);
	attr.name = "pagePointer_SlotNum";		attr.type = TypeInt;		attr.length = 4;			_indexNonLeafRecordDescriptor.push_back(attr);
	attr.name = "nextSlot_PageNum";			attr.type = TypeInt;		attr.length = 4;			_indexNonLeafRecordDescriptor.push_back(attr);
	attr.name = "nextSlot_SlotNum";			attr.type = TypeInt;		attr.length = 4;			_indexNonLeafRecordDescriptor.push_back(attr);
	attr.name = "keySize";					attr.type = TypeInt;		attr.length = 4;			_indexNonLeafRecordDescriptor.push_back(attr);
	attr.name = "key";						attr.type = TypeVarChar;	attr.length = MAX_KEY_SIZE;	_indexNonLeafRecordDescriptor.push_back(attr);

	// Index Leaf Record
	attr.name = "dataRid_PageNum";			attr.type = TypeInt;		attr.length = 4;			_indexLeafRecordDescriptor.push_back(attr);
	attr.name = "dataRid_SlotNum";			attr.type = TypeInt;		attr.length = 4;			_indexLeafRecordDescriptor.push_back(attr);
	attr.name = "nextSlot_PageNum";			attr.type = TypeInt;		attr.length = 4;			_indexLeafRecordDescriptor.push_back(attr);
	attr.name = "nextSlot_SlotNum";			attr.type = TypeInt;		attr.length = 4;			_indexLeafRecordDescriptor.push_back(attr);
	attr.name = "keySize";					attr.type = TypeInt;		attr.length = 4;			_indexLeafRecordDescriptor.push_back(attr);
	attr.name = "key";						attr.type = TypeVarChar;	attr.length = MAX_KEY_SIZE;	_indexLeafRecordDescriptor.push_back(attr);
}

IndexManager::~IndexManager()
{
    // We don't want our static pointer to be pointing to deleted data in case the object is ever deleted!
    _index_manager = NULL;
}

RC IndexManager::createFile(const string &fileName)
{
    return PagedFileManager::instance()->createFile(fileName.c_str());
}

RC IndexManager::destroyFile(const string &fileName)
{
    return PagedFileManager::instance()->destroyFile(fileName.c_str());
}

RC IndexManager::openFile(const string &fileName, FileHandle &fileHandle)
{
    return PagedFileManager::instance()->openFile(fileName.c_str(), fileHandle);
}

RC IndexManager::closeFile(FileHandle &fileHandle)
{
    return PagedFileManager::instance()->closeFile(fileHandle);
}

RC IndexManager::insertEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IndexManager::deleteEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IndexManager::scan(FileHandle &fileHandle,
    const Attribute &attribute,
    const void      *lowKey,
    const void      *highKey,
    bool			lowKeyInclusive,
    bool        	highKeyInclusive,
    IX_ScanIterator &ix_ScanIterator)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

IX_ScanIterator::IX_ScanIterator()
{
}

IX_ScanIterator::~IX_ScanIterator()
{
}

RC IX_ScanIterator::getNextEntry(RID &rid, void *key)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IX_ScanIterator::close()
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

void IX_PrintError (RC rc)
{
    std::cout << rc::rcToString(rc);
}
