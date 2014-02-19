#include "ix.h"
#include "../rbf/pfm.h"
#include "../util/returncodes.h"

#include <assert.h>
#include <cstring>

IndexManager* IndexManager::_index_manager = 0;

IndexManager* IndexManager::instance()
{
    if(!_index_manager)
        _index_manager = new IndexManager();

    return _index_manager;
}

IndexManager::IndexManager()
	: RecordBasedCoreManager(sizeof(IX_PageIndexFooter)), _pfm(*PagedFileManager::instance())
{
	// Index Non-Leaf Record
	Attribute attr;
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

	// Create the root page, mark it as a leaf, it's page 1
	_rootPageNum = 1;
	ret = newPage(fileHandle, _rootPageNum, true);
	if (ret != rc::OK)
	{
		return ret;
	}

	// We're done, leave the file closed
	ret = closeFile(fileHandle);
	if (ret != rc::OK)
	{
		return ret;
	}

	return rc::OK;
}

RC IndexManager::newPage(FileHandle& fileHandle, PageNum pageNum, bool isLeaf)
{
	IX_PageIndexFooter footer;
	footer.isLeafPage = isLeaf; 
	footer.firstRecord.pageNum = pageNum;
	footer.firstRecord.slotNum = 0;
	footer.parent = 0;
	footer.prevPage = 0;
	footer.nextPage = 0;
	footer.freeSpaceOffset = 0;
	footer.numSlots = 0;
	footer.gapSize = 0;
	footer.pageNumber = pageNum;

	// Append as many pages as needed (should be only 1)
	unsigned requiredPages = pageNum - fileHandle.getNumberOfPages() + 1;
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	for (unsigned i = 0; i < requiredPages; i++)
	{
		memset(pageBuffer, 0, PAGE_SIZE);
		writePageIndexFooter(pageBuffer, &footer);
		RC ret = fileHandle.appendPage(pageBuffer);
		if (ret != rc::OK)
		{
			cout << "FAIL" << endl;
			return ret;
		}
	}

	return rc::OK;
}

RC IndexManager::insertEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
	// Pull in the root page
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(_rootPageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Build the key struct for the index 
	KeyValueData keyData;
	ret = keyData.init(attribute.type, key);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Extract the header
	IX_PageIndexFooter* rootFooter = getIXPageIndexFooter(pageBuffer);

	// If the root is absolutely empty, insert the first leaf entry into the root
	// In this case, the new record is a leaf record
	if (rootFooter->numSlots == 0)
	{
		// Construct the new leaf record
		IndexLeafRecord leaf;
		leaf.dataRid = rid;
		leaf.nextSlot.pageNum = 0;
		leaf.nextSlot.slotNum = 0;
		leaf.key = keyData;
		
		// Insert the new record into the root
		RID newEntry;
		ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);
		if (ret != rc::OK)
		{
			return ret;
		}
		
		// Update the header of the page to point to this new entry
		rootFooter->firstRecord = newEntry;

		// Write the new page information to disk
		ret = fileHandle.writePage(_rootPageNum, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}
	else if (rootFooter->isLeafPage) // else, search the height=1 tree (just a root page) and figure out where it should go
	{
		bool found = false;
		bool atEnd = false;
		RID currRid = rootFooter->firstRecord;
		RID prevRid;
		IndexLeafRecord currEntry;
		IndexLeafRecord prevEntry;

		while (!found) // second condition implies the end of the chain
		{
			// Pull in the next entry
			ret = readRecord(fileHandle, _indexLeafRecordDescriptor, currRid, &currEntry);
			if (ret != rc::OK)
			{
				return ret;
			}

			int compareResult = 0;
			ret = keyData.compare(attribute.type, currEntry.key, compareResult);
			if (ret != rc::OK)
			{
				return ret;
			}

			if (compareResult < 0)
			{
				found = true;
			}
			else if (compareResult == 0)
			{
				// TODO: HANDLE EQUAL CASE
				found = true;
			}

			if (!found)
			{
				// Check to see if we reached the end of the list
				if (currEntry.nextSlot.pageNum != 0) 
				{
					atEnd = true;
					found = true;
				}
				prevRid = currRid;
				prevEntry = currEntry;
				currRid = currEntry.nextSlot;
			}
		}

		// Setup the entry to be inserted
		IndexLeafRecord leaf;
		leaf.dataRid = rid;
		leaf.nextSlot.pageNum = 0;
		leaf.nextSlot.slotNum = 0;
		leaf.key = keyData;

		// Compute freespace left so as to determine if we'll be splitting or not
		unsigned targetFreeSpace = calculateFreespace(rootFooter->freeSpaceOffset, rootFooter->numSlots);
		unsigned entryRecordSize = calcRecordSize((unsigned char*)(&leaf));

		// if we can squeeze on this page, put it in the right spot 
		if (entryRecordSize < targetFreeSpace)
		{
			if (!atEnd) // start or middle of list
			{
				// Insert the new record into the root, and make it point to the current RID
				leaf.nextSlot = currRid;
				RID newEntry;
				insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);

				// Update the previous entry to point to the new entry (it's in the middle now)
				prevEntry.nextSlot = newEntry;
				ret = updateRecord(fileHandle, _indexLeafRecordDescriptor, &prevEntry, prevRid);
				if (ret != rc::OK)
				{
					return ret;
				}
			}
			else // append the RID to the end of the on-page list 
			{
				// Insert the new record into the root
				RID newEntry;
				insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);

				// Update the previous entry to point to the new entry
				prevEntry.nextSlot = newEntry;
				ret = updateRecord(fileHandle, _indexLeafRecordDescriptor, &prevEntry, prevRid);
				if (ret != rc::OK)
				{
					return ret;
				}
			}

			// Write the new page information to disk
			ret = fileHandle.writePage(_rootPageNum, pageBuffer);
			if (ret != rc::OK)
			{
				return ret;
			}
		}
		else // SPLIT CONDITION
		{
			return rc::FEATURE_NOT_YET_IMPLEMENTED;
		}
	}
	else // else, the root is a non-leaf, and we have the general search here
	{
		return rc::FEATURE_NOT_YET_IMPLEMENTED;
	}

    return rc::OK;
}

RC IndexManager::deleteEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

PageNum IndexManager::split(FileHandle& fileHandle, PageNum target)
{
	// Read in the page to be split
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(_rootPageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Extract the header
	IX_PageIndexFooter* targetFooter = getIXPageIndexFooter(pageBuffer);
	assert(targetFooter->numSlots > 0); 

	// Allocate the new page
	PageNum newPageNum = fileHandle.getNumberOfPages();
	newPage(fileHandle, newPageNum, targetFooter->isLeafPage);

	// Read in half of the entries from the target page and insert them onto the new page
	RID currRid = targetFooter->firstRecord;
	unsigned numToMove = (targetFooter->numSlots / 2);
	for (unsigned i = 0; i < numToMove; i++)
	{
		// Skip over the first 'numToMove' RIDs since they stay in place
		if (targetFooter->isLeafPage)
		{
			IndexLeafRecord lRecord;
			readRecord(fileHandle, _indexLeafRecordDescriptor, currRid, &lRecord);
			currRid = lRecord.nextSlot;
		}
		else
		{
			IndexNonLeafRecord nlRecord;	
			readRecord(fileHandle, _indexNonLeafRecordDescriptor, currRid, &nlRecord);
			currRid = nlRecord.nextSlot;
		}
		
	}

	// Save a reference to the split spot so the disconnect can be made later
	RID splitRid = currRid;

	// The currRid variable now points to the correct spot in the list from which we should start moving
	// Move the rest over to the new page
	for (unsigned i = numToMove; i < targetFooter->numSlots; i++)
	{
		// Move the entry over to the new page
		RID newEntry;
		if (targetFooter->isLeafPage)
		{
			IndexLeafRecord lRecord;
			readRecord(fileHandle, _indexLeafRecordDescriptor, currRid, &lRecord);
			insertRecord(fileHandle, _indexLeafRecordDescriptor, &lRecord, newEntry);
			currRid = lRecord.nextSlot;
		}
		else
		{
			IndexNonLeafRecord nlRecord;	
			readRecord(fileHandle, _indexNonLeafRecordDescriptor, currRid, &nlRecord);
			insertRecord(fileHandle, _indexNonLeafRecordDescriptor, &nlRecord, newEntry);
			currRid = nlRecord.nextSlot;
		}
	}

	// Make the disconnect by terminating the on-page list on the first (source) page
	if (targetFooter->isLeafPage)
	{
		IndexLeafRecord lRecord;
		readRecord(fileHandle, _indexLeafRecordDescriptor, splitRid, &lRecord);
		lRecord.nextSlot.pageNum = 0;
		lRecord.nextSlot.slotNum = 0;
		ret = updateRecord(fileHandle, _indexLeafRecordDescriptor, &lRecord, splitRid);
		if (ret != rc::OK)
		{
			return ret;
		}
	}
	else
	{
		IndexNonLeafRecord nlRecord;	
		readRecord(fileHandle, _indexNonLeafRecordDescriptor, splitRid, &nlRecord);
		nlRecord.nextSlot.pageNum = 0;
		nlRecord.nextSlot.slotNum = 0;
		ret = updateRecord(fileHandle, _indexNonLeafRecordDescriptor, &nlRecord, splitRid);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	return newPageNum;
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

IX_PageIndexFooter* IndexManager::getIXPageIndexFooter(void* pageBuffer)
{
	return (IX_PageIndexFooter*)getCorePageIndexFooter(pageBuffer);
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

RC KeyValueData::init(AttrType type, const void* key)
{
	// Build the key struct for the index 
	switch (type)
	{
		case TypeInt:
			size = sizeof(unsigned);
			memcpy(&integer, key, sizeof(unsigned));

		case TypeReal:
			size = sizeof(float);
			memcpy(&real, key, sizeof(float));
			break;

		case TypeVarChar:
			memcpy(&size, key, sizeof(unsigned));
			memcpy(&varchar, (char*)key + sizeof(unsigned), size);
			break;

		default:
			return rc::ATTRIBUTE_INVALID_TYPE;
	}

	return rc::OK;
}

RC KeyValueData::compare(AttrType type, const KeyValueData& that, int& result)
{
	switch (type)
	{
		case TypeInt:
			result = integer - that.integer;
			break;

		case TypeReal:
			if (real < that.real)
				result = -1;
			else if (real == that.real)
				result = 0;
			else
				result = 1;
			break;

		case TypeVarChar:
			// TODO: Handle "AAA" vs "AAAAA" where only lengths are unequal
			result = strncmp(varchar, that.varchar, size);
			break;

		default:
			return rc::ATTRIBUTE_INVALID_TYPE;
	}

	return rc::OK;
}
