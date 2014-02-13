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
	RC ret = RecordBasedFileManager::instance()->createFile(fileName.c_str());
	if (ret != rc::OK)
	{
		return ret;
	}

	// Open up the file so we can initialize it with some data
	FileHandle fileHandle;
	ret = openFile(fileName, fileHandle);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Insert the index header as the 1st record
	

	RID rid;
	ret = newPage(fileHandle, rid);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (rid.pageNum != 1)
	{
		return rc::INDEX_PAGE_INITIALIZATION_FAILED;
	}

	// We're done, leave the file closed
	ret = closeFile(fileHandle);
	if (ret != rc::OK)
	{
		return ret;
	}

	return rc::OK;
}

RC IndexManager::newPage(FileHandle& fileHandle, RID& headerRid)
{
	IndexHeader header;
	header.isLeafPage = false;
	header.firstRecord.pageNum = 0;
	header.firstRecord.slotNum = 0;
	header.parent = 0;
	header.prev = 0;
	header.next = 0;

	// TODO: We need a way to tell RBFM to force a new page
	RC ret = RecordBasedFileManager::instance()->insertRecord(fileHandle, _indexHeaderDescriptor, &header, headerRid);
	if (ret != rc::OK)
	{
		return ret;
	}

	// We have just created the file, so we should be garunteed to have the 1st page and 1st slot
	if (headerRid.slotNum != 0)
	{
		return rc::INDEX_PAGE_INITIALIZATION_FAILED;
	}

	return rc::OK;
}

RC IndexManager::destroyFile(const string &fileName)
{
    return RecordBasedFileManager::instance()->destroyFile(fileName.c_str());
}

RC IndexManager::openFile(const string &fileName, FileHandle &fileHandle)
{
    return RecordBasedFileManager::instance()->openFile(fileName.c_str(), fileHandle);
}

RC IndexManager::closeFile(FileHandle &fileHandle)
{
    return RecordBasedFileManager::instance()->closeFile(fileHandle);
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
	return ix_ScanIterator.init(&fileHandle, attribute, lowKey, highKey, lowKeyInclusive, highKeyInclusive);
}

IX_ScanIterator::IX_ScanIterator()
	: _fileHandle(NULL), 
	_attribute(),
	_lowKeyValue(NULL), 
	_highKeyValue(NULL), 
	_lowKeyInclusive(false), 
	_highKeyInclusive(false),
	_currentRid()
{
}

IX_ScanIterator::~IX_ScanIterator()
{
	close();
}

RC IX_ScanIterator::init(FileHandle* fileHandle, const Attribute &attribute, const void *lowKey, const void *highKey, bool lowKeyInclusive, bool highKeyInclusive)
{
	if (!fileHandle)
		return rc::FILE_HANDLE_NOT_INITIALIZED;

	if (attribute.length == 0)
		return rc::ATTRIBUTE_LENGTH_INVALID;

	_fileHandle = fileHandle;
	_attribute = attribute;
	_lowKeyInclusive = lowKeyInclusive;
	_highKeyInclusive = highKeyInclusive;

	// TODO: Find the first record
	_currentRid.pageNum = 1;
	_currentRid.slotNum = 0;

	// Copy over the key values to local memory
	if (lowKey)
	{
		RC ret = Attribute::allocateValue(attribute.type, lowKey, &_lowKeyValue);
		if (ret != rc::OK)
		{
			return ret;
		}
	}
	else
	{
		if (_lowKeyValue)
			free(_lowKeyValue);

		_lowKeyValue = NULL;
	}

	if (highKey)
	{
		RC ret = Attribute::allocateValue(attribute.type, highKey, &_highKeyValue);
		if (ret != rc::OK)
		{
			return ret;
		}
	}
	else
	{
		if (_highKeyValue)
			free(_highKeyValue);

		_highKeyValue = NULL;
	}

	return rc::OK;
}

RC IX_ScanIterator::getNextEntry(RID &rid, void *key)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IX_ScanIterator::close()
{
	if (_lowKeyValue)
		free(_lowKeyValue);

	if (_highKeyValue)
		free(_highKeyValue);

	_fileHandle = NULL; 
	_lowKeyValue = NULL;
	_highKeyValue = NULL; 

	return rc::OK;
}

void IX_PrintError (RC rc)
{
    std::cout << rc::rcToString(rc);
}
