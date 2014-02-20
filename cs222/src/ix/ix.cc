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
	ret = newPage(fileHandle, _rootPageNum, true, 0);
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

RC IndexManager::newPage(FileHandle& fileHandle, PageNum pageNum, bool isLeaf, PageNum nextLeafPage)
{
	IX_PageIndexFooter footerTemplate;
	footerTemplate.isLeafPage = isLeaf; 
	footerTemplate.firstRecord.pageNum = pageNum;
	footerTemplate.firstRecord.slotNum = 0;
	footerTemplate.parent = 0;
	footerTemplate.nextLeafPage = nextLeafPage;
	footerTemplate.freespacePrevPage = 0;
	footerTemplate.freespaceNextPage = 0;
	footerTemplate.freeSpaceOffset = 0;
	footerTemplate.numSlots = 0;
	footerTemplate.gapSize = 0;
	footerTemplate.pageNumber = pageNum;
	footerTemplate.leftChild = 0;

	// TODO: Check freespace list to see if there's a completely empty page available

	// Append as many pages as needed (should be only 1)
	unsigned requiredPages = pageNum - fileHandle.getNumberOfPages() + 1;
	unsigned char pageBuffer[PAGE_SIZE];
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	for (unsigned i = 0; i < requiredPages; i++)
	{
		memset(pageBuffer, 0, PAGE_SIZE);
		memcpy(footer, &footerTemplate, sizeof(footerTemplate));

		RC ret = fileHandle.appendPage(pageBuffer);
		if (ret != rc::OK)
		{
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
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// Traverse down the tree to the leaf, using non-leaves along the way
	PageNum nextPage = _rootPageNum;
	while (footer->isLeafPage == false)
	{
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, &keyData, nextPage);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Pull the designated page into memory
		memset(pageBuffer, 0, PAGE_SIZE);
		ret = fileHandle.readPage(nextPage, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Update the header
		footer = getIXPageIndexFooter(pageBuffer);
	}

	// We're at a leaf now...
	assert(footer->isLeafPage);

	// cout << "leaf page = " << nextPage << endl;
	ret = insertIntoLeaf(fileHandle, nextPage, attribute, keyData, rid);
	if (ret != rc::OK && ret != rc::INDEX_PAGE_FULL)
	{
		cout << "failed: " << rc::rcToString(ret) << endl;
		return ret;
	}
	else
	{
		while (ret == rc::INDEX_PAGE_FULL)
		{
			// Split the page (no difference between leaves and non-leaves)
			PageNum leftPage = nextPage;
			PageNum rightPage;
			KeyValueData rightKey;
			RID rightRid;
			cout << "Split like a ballerina" << endl;
			ret = split(fileHandle, leftPage, rightPage, rightRid, rightKey);
			if (ret != rc::OK)
			{
				return ret;
			}

			// Check to see if we need to grow by one level, and if so, add a new page with enough space to 
			// hold the new index entry
			if (nextPage == _rootPageNum)
			{
				PageNum oldRoot = _rootPageNum;
				_rootPageNum = fileHandle.getNumberOfPages();
				ret = newPage(fileHandle, fileHandle.getNumberOfPages(), false, 0);
				if (ret != rc::OK)
				{
					return ret;
				}

				// Update the left/right children parents to point to the new root
				unsigned char tempBuffer[PAGE_SIZE] = {0};
				ret = fileHandle.readPage(leftPage, tempBuffer);
				if (ret != rc::OK)
				{
					return ret;
				}
				IX_PageIndexFooter* tempFooter = getIXPageIndexFooter(tempBuffer);
				tempFooter->parent = _rootPageNum;

				// Re-update the reference to the new footer to be used in the -final- insertion
				footer = tempFooter;

				// Write the new page information to disk
				ret = fileHandle.writePage(leftPage, tempBuffer);
				if (ret != rc::OK)
				{
					return ret;
				}

				memset(tempBuffer, 0, PAGE_SIZE);
				ret = fileHandle.readPage(rightPage, tempBuffer);
				if (ret != rc::OK)
				{
					return ret;
				}
				tempFooter = getIXPageIndexFooter(tempBuffer);
				tempFooter->parent = _rootPageNum;

				// Write the new page information to disk
				ret = fileHandle.writePage(rightPage, tempBuffer);
				if (ret != rc::OK)
				{
					return ret;
				}

				cout << "WE GREW!" << endl;
			}

			// Insert the right child into the parent of the left
			PageNum parent = footer->parent;
			ret = insertIntoNonLeaf(fileHandle, parent, attribute, rightKey, rightRid);
			if (ret != rc::OK && ret != rc::INDEX_PAGE_FULL)
			{
				return ret;
			}
			nextPage = parent;
		}
	}
	
	return rc::OK;
}

RC IndexManager::deleteEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
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
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// Traverse down the tree to the leaf, using non-leaves along the way
	PageNum nextPage;
	while (footer->isLeafPage == false)
	{
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, &keyData, nextPage);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Pull the designated page into memory
		memset(pageBuffer, 0, PAGE_SIZE);
		ret = fileHandle.readPage(nextPage, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Update the header
		footer = getIXPageIndexFooter(pageBuffer);
	}

	// Now search along the leaf page
	assert(footer->isLeafPage);
	RID entryRid;
	RID dataRid;
	ret = findLeafIndexEntry(fileHandle, footer, attribute, &keyData, entryRid, dataRid);
	if (ret != rc::OK)
	{
		return ret;
	}

    cout << "Entry RID: " << entryRid.pageNum << "," << entryRid.slotNum << endl;
    cout << "Data RID: " << dataRid.pageNum << "," << dataRid.slotNum << endl;

    // Delete the entry from the index now
    ret = deleteRecord(fileHandle, _indexLeafRecordDescriptor, entryRid);
    if (ret != rc::OK)
    {
        cout << rc::rcToString(ret) << endl;
        return ret;
    }

    // TODO: check for merge here

    return rc::OK;
}

RC IndexManager::insertIntoNonLeaf(FileHandle& fileHandle, PageNum& page, const Attribute &attribute, KeyValueData keyData, RID rid)
{
	// Pull in the root page and then extract the header
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(page, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	IndexNonLeafRecord entry;
	entry.pagePointer = rid;
	entry.nextSlot.pageNum = 0;
	entry.nextSlot.slotNum = 0;
	entry.key = keyData;

	// Determine if we can fit on this page
	unsigned targetFreeSpace = calculateFreespace(footer->freeSpaceOffset, footer->numSlots);
	unsigned entryRecordSize = calcRecordSize((unsigned char*)(&entry));
	if (entryRecordSize > targetFreeSpace)
	{
		return rc::INDEX_PAGE_FULL;
	}
	else // if we can fit, find the spot in the list where the new record will go
	{
		bool found = false;
		bool atEnd = false;
		RID currRid = footer->firstRecord;
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

		// Drop the record into the right spot in the on-page list
		if (!atEnd) // start or middle of list
		{
			// Insert the new record into the root, and make it point to the current RID
			entry.nextSlot = currRid;
			RID newEntry;
			ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &entry, newEntry);
			if (ret != rc::OK)
			{
				return ret;
			}

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
			ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &entry, newEntry);
			if (ret != rc::OK)
			{
				return ret;
			}

			// Update the previous entry to point to the new entry
			prevEntry.nextSlot = newEntry;
			ret = updateRecord(fileHandle, _indexLeafRecordDescriptor, &prevEntry, prevRid);
			if (ret != rc::OK)
			{
				return ret;
			}
		}

		// Write the new page information to disk
		ret = fileHandle.writePage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	return rc::OK;
}

RC IndexManager::insertIntoLeaf(FileHandle& fileHandle, PageNum& page, const Attribute &attribute, KeyValueData keyData, RID rid)
{
	// Pull in the root page and then extract the header
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(page, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// Special case if the leaf is empty
	if (footer->numSlots == 0)
	{
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
		footer->firstRecord = newEntry;
		// cout << "inserted RID: " << newEntry.pageNum << "," << newEntry.slotNum << endl;

		// Write the new page information to disk
		ret = fileHandle.writePage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}
	else
	{
		IndexLeafRecord leaf;
		leaf.dataRid = rid;
		leaf.nextSlot.pageNum = 0;
		leaf.nextSlot.slotNum = 0;
		leaf.key = keyData;

		// Determine if we can fit on this page
		unsigned targetFreeSpace = calculateFreespace(footer->freeSpaceOffset, footer->numSlots);
		unsigned entryRecordSize = calcRecordSize((unsigned char*)(&leaf));
		if (entryRecordSize > targetFreeSpace)
		{
			return rc::INDEX_PAGE_FULL;
		}
		else // if we can fit, find the spot in the list where the new record will go
		{
			bool found = false;
			bool atEnd = false;
			RID currRid = footer->firstRecord;
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

			// Drop the record into the right spot in the on-page list
			if (!atEnd) // start or middle of list
			{
				// Insert the new record into the root, and make it point to the current RID
				leaf.nextSlot = currRid;
				RID newEntry;
				ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);
				if (ret != rc::OK)
				{
					return ret;
				}

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
				ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);
				if (ret != rc::OK)
				{
					return ret;
				}

				// Update the previous entry to point to the new entry
				prevEntry.nextSlot = newEntry;
				ret = updateRecord(fileHandle, _indexLeafRecordDescriptor, &prevEntry, prevRid);
				if (ret != rc::OK)
				{
					return ret;
				}
			}

			// Write the new page information to disk
			ret = fileHandle.writePage(page, pageBuffer);
			if (ret != rc::OK)
			{
				return ret;
			}
		}
	}

	return rc::OK;
}

RC IndexManager::findNonLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, PageNum& pageNum)
{
	RID targetRid;
	RID prevRid;
	int compareResult = -1;

	// Extract the first record
	RID currRid = footer->firstRecord;
	IndexNonLeafRecord currEntry;
	IndexNonLeafRecord prevEntry;

	// Traverse the list of index entries on this non-leaf page
	while (compareResult < 0 && currRid.pageNum > 0)
	{
		// Pull in the new entry and perform the comparison
		readRecord(fileHandle, _indexNonLeafRecordDescriptor, currRid, &currEntry);
		RC ret = key->compare(attribute.type, currEntry.key, compareResult);
		if (ret != rc::OK)
		{
			return ret;
		}

		if (compareResult >= 0)
		{
			targetRid = currEntry.pagePointer;
		}
		else
		{
			prevRid = targetRid;
			prevEntry = currEntry;
			currRid = currEntry.nextSlot;
		}
	}

	// Check to see if we traverse the list entirely, in which case the last index entry 
	// contains the page pointer to which we point
	if (currRid.pageNum == 0)
	{
		targetRid = prevEntry.pagePointer;
	}

	// Save the result and return OK
	pageNum = targetRid.pageNum;
	return rc::OK;
}

RC IndexManager::findLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, RID& entryRid, RID& targetRid)
{
	RID prevRid;
	int compareResult = -1;

	// Extract the first record
	RID currRid = footer->firstRecord;
	IndexLeafRecord currEntry;
	IndexLeafRecord prevEntry;

	// Traverse the list of index entries on this leaf page
	while (compareResult < 0 && currRid.pageNum > 0)
	{
		// Pull in the new entry and perform the comparison
		readRecord(fileHandle, _indexLeafRecordDescriptor, currRid, &currEntry);
		RC ret = key->compare(attribute.type, currEntry.key, compareResult);
		if (ret != rc::OK)
		{
			return ret;
		}

		// If we have a match, save it and return OK, otherwise keep traversing the entries on the leaf page
		if (compareResult == 0)
		{
			entryRid = currRid;
			targetRid = currEntry.dataRid;
		}
		else
		{
			prevRid = targetRid;
			prevEntry = currEntry;
			currRid = currEntry.nextSlot;
		}
	}

	// Check to see if we traverse the list entirely, in which case the last index entry 
	// contains the page pointer to which we point
	if (currRid.pageNum == 0)
	{
		return rc::INDEX_LEAF_ENTRY_NOT_FOUND;
	}
	else
	{
		return rc::OK;
	}
}

RC IndexManager::split(FileHandle& fileHandle, PageNum& targetPageNum, PageNum& newPageNum, RID& rightRid, KeyValueData& rightKey)
{
	// Read in the page to be split
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(targetPageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Extract the header
	IX_PageIndexFooter* targetFooter = getIXPageIndexFooter(pageBuffer);
	assert(targetFooter->numSlots > 0); 

	// Allocate the new page and save its reference
	newPageNum = fileHandle.getNumberOfPages();
	ret = newPage(fileHandle, newPageNum, targetFooter->isLeafPage, targetFooter->nextLeafPage);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Update the nextLeaf pointer if needed
	if (targetFooter->isLeafPage)
	{
		targetFooter->nextLeafPage = newPageNum;
	}

	// Save the parent of the new footer
	unsigned char newPageBuffer[PAGE_SIZE] = {0};
	ret = fileHandle.readPage(newPageNum, newPageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	IX_PageIndexFooter* newFooter = getIXPageIndexFooter(newPageBuffer);
	newFooter->parent = targetFooter->parent;

	// Write the new page information to disk
	ret = fileHandle.writePage(newPageNum, newPageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

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

			// Save the new key
			if (i == numToMove)
			{
				rightRid = newEntry;
				rightKey = lRecord.key;
			}
		}
		else
		{
			IndexNonLeafRecord nlRecord;	
			readRecord(fileHandle, _indexNonLeafRecordDescriptor, currRid, &nlRecord);
			insertRecord(fileHandle, _indexNonLeafRecordDescriptor, &nlRecord, newEntry);
			currRid = nlRecord.nextSlot;

			// Save the new key
			if (i == numToMove)
			{
				rightRid = newEntry;
				rightKey = nlRecord.key;
			}
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

	return rc::OK;
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

RC IndexManager::copyRecordsInplace(FileHandle& fileHandle, const std::vector<Attribute>& recordDescriptor, void* inputBuffer, void* outputBuffer, PageNum outputPageNum)
{
    IX_PageIndexFooter* inputFooter = getIXPageIndexFooter(inputBuffer);

    unsigned numRecords = 0;
    RID currentRid = inputFooter->firstRecord;
    RID writtenRid;

    IndexRecordOverlap currentRecord;
    IndexCommonRecord* commonRecord = (IndexCommonRecord*)&currentRecord;

    while(currentRid.pageNum != 0)
    {
        RC ret = readRecord(fileHandle, recordDescriptor, currentRid, &currentRecord, inputBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // We assume inserting into a fresh page, so we know what the next slot will be
        currentRid = commonRecord->nextSlot;
        if (currentRid.pageNum == 0)
        {
            commonRecord->nextSlot.slotNum = 0;
            commonRecord->nextSlot.pageNum = 0;
        }
        else
        {
            commonRecord->nextSlot.slotNum = numRecords + 1;
            commonRecord->nextSlot.pageNum = outputPageNum;
        }

        ret = insertRecordInplace(recordDescriptor, &currentRecord, outputPageNum, outputBuffer, writtenRid);
        if (ret != rc::OK)
        {
            return ret;
        }

        ++numRecords;
    }

    return rc::OK;
}

/*
RC IndexManager::freePage(FileHandle& fileHandle, IX_PageIndexFooter* footer, void* pageBuffer)
{
    PageNum pageNum = footer->pageNumber;
    unsigned freespaceList = footer->freespaceList;

    memset(pageBuffer, 0, PAGE_SIZE);
    footer->pageNumber = pageNum;
    footer->freespaceList = freespaceList;

    RC ret = fileHandle.writePage(pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    ret = movePageToCorrectFreeSpaceList(fileHandle, footer);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::OK;
}

RC IndexManager::mergePages(FileHandle& fileHandle, const Attribute &attribute, PageNum leaf1Page, PageNum leaf2Page, PageNum destinationPage)
{
    const unsigned numPages = fileHandle.getNumberOfPages();
    if (leaf1Page >= numPages || leaf2Page >= numPages || destinationPage >= numPages)
    {
        return rc::PAGE_NUM_INVALID;
    }

    RC ret = rc::OK;
    char page1Buffer[PAGE_SIZE] = {0};
    char page2Buffer[PAGE_SIZE] = {0};

    ret = fileHandle.readPage(leaf1Page, page1Buffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    ret = fileHandle.readPage(leaf2Page, page2Buffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Get the basic data about pages we are merging and ensure we have enough space
    IX_PageIndexFooter* footer1 = getIXPageIndexFooter(page1Buffer);
    IX_PageIndexFooter* footer2 = getIXPageIndexFooter(page2Buffer);

    if (calculateFreespace(footer1->freeSpaceOffset + footer2->freeSpaceOffset, footer1->numSlots + footer2->numSlots) > PAGE_SIZE)
    {
        return rc::BTREE_CANNOT_MERGE_PAGES_TOO_FULL;
    }

    const bool isLeaf = footer1->isLeafPage;
    if (isLeaf != footer2->isLeafPage)
    {
        return rc::BTREE_CANNOT_MERGE_LEAF_AND_NONLEAF;
    }

    // Get the information about the parents of the pages we're merging
    char parent1Buffer[PAGE_SIZE] = {0};
    char parent2Buffer[PAGE_SIZE] = {0};

    ret = fileHandle.readPage(footer1->parent, parent1Buffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    ret = fileHandle.readPage(footer2->parent, parent2Buffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Grab the key that points to the left page
    IndexRecordOverlap firstRecord1, firstRecord2;
    const std::vector<Attribute>& recordDescriptor = isLeaf ? _indexLeafRecordDescriptor : _indexNonLeafRecordDescriptor;

    ret = readRecord(fileHandle, recordDescriptor, footer1->firstRecord, &firstRecord1, page1Buffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    ret = readRecord(fileHandle, recordDescriptor, footer2->firstRecord, &firstRecord2, page2Buffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // TODO: Fix up prev/next page pointers


    // Fill out the footer information for the output buffer
    char outputBuffer[PAGE_SIZE] = {0};
    IX_PageIndexFooter* outputFooter = getIXPageIndexFooter(outputBuffer);
    outputFooter->firstRecord.pageNum = destinationPage;
    outputFooter->firstRecord.slotNum = 0;
    outputFooter->freeSpaceOffset = 0;
    outputFooter->freespaceList = 0;
    outputFooter->gapSize = 0;
    outputFooter->isLeafPage = isLeaf;
    outputFooter->leftChild = footer1->leftChild;
    outputFooter->freespaceNextPage = 0;
    outputFooter->freespacePrevPage = 0;
    outputFooter->numSlots = 0;
    outputFooter->pageNumber = destinationPage;
    outputFooter->parent = footer1->parent;
	outputFooter->nextLeafPage = footer2->nextLeafPage;
	outputFooter->prevLeafPage = footer1->prevLeafPage;

    // Bulk copy all records of the first page to the new output buffer
    ret = copyRecordsInplace(fileHandle, recordDescriptor, page1Buffer, outputBuffer, destinationPage);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Bulk copy all records of the second page to the new output bufferr
    ret = copyRecordsInplace(fileHandle, recordDescriptor, page2Buffer, outputBuffer, destinationPage);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Free up the pages
    if (leaf1Page != destinationPage)
    {
        ret = freePage(fileHandle, footer1, page1Buffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }

    if (leaf2Page != destinationPage)
    {
        ret = freePage(fileHandle, footer2, page2Buffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }

    // Write to the output buffer
    ret = fileHandle.writePage(destinationPage, outputBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Ensure output buffer is on correct freepsace list (since our bulk insert did not update it
    ret = movePageToCorrectFreeSpaceList(fileHandle, outputFooter);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}
*/

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
