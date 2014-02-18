#include "ix.h"
#include "../rbf/pfm.h"
#include "../util/returncodes.h"

#include <cstring>

IndexManager* IndexManager::_index_manager = 0;

IndexManager* IndexManager::instance()
{
    if(!_index_manager)
        _index_manager = new IndexManager();

    return _index_manager;
}

IndexManager::IndexManager()
	: RecordBasedCoreManager(sizeof(IX_PageIndexHeader)), _pfm(*PagedFileManager::instance())
{
	// Index Header
	Attribute attr;
	attr.name = "isLeafPage";				attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "firstRecord_PageNum";		attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "firstRecord_SlotNum";		attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "parent_PageNum";			attr.type = TypeInt;	attr.length = 4;	_indexHeaderDescriptor.push_back(attr);
	attr.name = "numSlots";                 attr.type = TypeInt;    attr.length = 4;    _indexHeaderDescriptor.push_back(attr);
	attr.name = "gapSize";                  attr.type = TypeInt;    attr.length = 4;    _indexHeaderDescriptor.push_back(attr);
	attr.name = "pageNumber";               attr.type = TypeInt;    attr.length = 4;    _indexHeaderDescriptor.push_back(attr);
	attr.name = "freespaceList";            attr.type = TypeInt;    attr.length = 4;    _indexHeaderDescriptor.push_back(attr);
	attr.name = "prevPage";                 attr.type = TypeInt;    attr.length = 4;    _indexHeaderDescriptor.push_back(attr);
	attr.name = "nextPage";                 attr.type = TypeInt;    attr.length = 4;    _indexHeaderDescriptor.push_back(attr);

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

RC IndexManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IndexManager::updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IndexManager::readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IndexManager::reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

unsigned IndexManager::calcRecordSize(unsigned char* recordBuffer)
{
	return 0;
}

RC IndexManager::createFile(const string &fileName)
{
	RC ret = _pfm.createFile(fileName.c_str());
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

	// Insert the index header as the 1st record on the first page
	ret = newPage(fileHandle, _rootHeaderRid, 1);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_rootHeaderRid.pageNum != 1)
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

RC IndexManager::newPage(FileHandle& fileHandle, RID& headerRid, PageNum pageNum)
{
	IX_PageIndexHeader header;
	header.isLeafPage = false; // TODO: this should really be passed in as a parameter
	header.firstRecord.pageNum = pageNum;
	header.firstRecord.slotNum = 1; 
	header.parent = 0;
	header.core.prevPage = 0;
	header.core.nextPage = 0;
	header.core.freeSpaceOffset = 0;
	header.core.numSlots = 0;
	header.core.gapSize = 0;
	header.core.pageNumber = pageNum;

	// Append as many pages as needed
	unsigned requiredPages = pageNum - fileHandle.getNumberOfPages() + 1;
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	memset(pageBuffer, 0, PAGE_SIZE);
	for (unsigned i = 0; i < requiredPages; i++)
	{
		fileHandle.appendPage(pageBuffer);
	}

	// Insert the header entry on the specified page
	RC ret = insertRecordToPage(fileHandle, _indexHeaderDescriptor, &header, pageNum, headerRid);
	if (ret != rc::OK)
	{
		return ret;
	}

	IX_PageIndexHeader testheader;
	ret = readRecord(fileHandle, _indexHeaderDescriptor, _rootHeaderRid, &testheader);
	cout << testheader.firstRecord.pageNum << " " << testheader.firstRecord.slotNum << endl;
	cout << _rootHeaderRid.pageNum << " " << _rootHeaderRid.slotNum << endl;

	// We have just created the file, so we should be garunteed to have the 1st page and 1st slot
	if (headerRid.slotNum != 0)
	{
		return rc::INDEX_PAGE_INITIALIZATION_FAILED;
	}

	return rc::OK;
}

RC IndexManager::insertEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
	// Pull in the root header
	IX_PageIndexHeader header;
	RC ret = readRecord(fileHandle, _indexHeaderDescriptor, _rootHeaderRid, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

	// if index entry is empty, this is one of the first to be added, so allocate a new leaf page and set up the root/leaf
	// else, read in first rid

	// Read in the first record on the page - this will NEVER be a leaf
	IndexNonLeafRecord nlRecord;
	ret = readRecord(fileHandle, _indexNonLeafRecordDescriptor, header.firstRecord, &nlRecord);
	if (ret != rc::OK)
	{
		return ret;
	}

	// walk the list of records starting with header.firstRecord and find out where to go

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
	_currentRid(),
	_pfm(*PagedFileManager::instance())
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
