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
	attr.name = "key";						attr.type = TypeInt;	    attr.length = 4;	        _indexLeafRecordDescriptor.push_back(attr);

	// Index Leaf Record
	attr.name = "dataRid_PageNum";			attr.type = TypeInt;		attr.length = 4;			_indexVarCharLeafRecordDescriptor.push_back(attr);
	attr.name = "dataRid_SlotNum";			attr.type = TypeInt;		attr.length = 4;			_indexVarCharLeafRecordDescriptor.push_back(attr);
	attr.name = "nextSlot_PageNum";			attr.type = TypeInt;		attr.length = 4;			_indexVarCharLeafRecordDescriptor.push_back(attr);
	attr.name = "nextSlot_SlotNum";			attr.type = TypeInt;		attr.length = 4;			_indexVarCharLeafRecordDescriptor.push_back(attr);
	attr.name = "keySize";					attr.type = TypeInt;		attr.length = 4;			_indexVarCharLeafRecordDescriptor.push_back(attr);
	attr.name = "key";						attr.type = TypeVarChar;	attr.length = MAX_KEY_SIZE;	_indexVarCharLeafRecordDescriptor.push_back(attr);
}

IndexManager::~IndexManager()
{
    // We don't want our static pointer to be pointing to deleted data in case the object is ever deleted!
    _index_manager = NULL;
}

// RC IndexManager::updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid)
// {
// 	return rc::FEATURE_NOT_YET_IMPLEMENTED;
// }

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
	ret = newPage(fileHandle, _rootPageNum, true, 0, 0);
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

RC IndexManager::newPage(FileHandle& fileHandle, PageNum pageNum, bool isLeaf, PageNum nextLeafPage, PageNum leftChild)
{
	IX_PageIndexFooter footerTemplate;
	footerTemplate.isLeafPage = isLeaf; 
	footerTemplate.firstRecord.pageNum = pageNum;
	footerTemplate.firstRecord.slotNum = 0;
	footerTemplate.parent = 0;
	footerTemplate.nextLeafPage = nextLeafPage;
	footerTemplate.core.freespacePrevPage = 0;
	footerTemplate.core.freespaceNextPage = 0;
	footerTemplate.core.freeSpaceOffset = 0;
	footerTemplate.core.numSlots = 0;
	footerTemplate.core.gapSize = 0;
	footerTemplate.core.pageNumber = pageNum;
	footerTemplate.leftChild = leftChild;
	footerTemplate.core.freespaceList = NUM_FREESPACE_LISTS - 1;

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

	ret = insertIntoLeaf(fileHandle, nextPage, attribute, keyData, rid);
	if (ret != rc::OK && ret != rc::BTREE_INDEX_PAGE_FULL)
	{
		return ret;
	}
	else
	{
		while (ret == rc::BTREE_INDEX_PAGE_FULL)
		{
			// Split the page (no difference between leaves and non-leaves)
			PageNum leftPage = nextPage;
			PageNum rightPage;
			KeyValueData rightKey;
			RID rightRid;
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
				ret = newPage(fileHandle, fileHandle.getNumberOfPages(), false, 0, leftPage);
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
				memcpy(tempBuffer + PAGE_SIZE - sizeof(IX_PageIndexFooter), tempFooter, sizeof(IX_PageIndexFooter));

				// Re-update the reference to the new footer to be used in the -final- insertion
				footer = tempFooter;

				// Write the new page information to disk
				ret = fileHandle.writePage(leftPage, tempBuffer);
				if (ret != rc::OK)
				{
					return ret;
				}

				// Reset the buffer and then read in the right page
				memset(tempBuffer, 0, PAGE_SIZE);
				ret = fileHandle.readPage(rightPage, tempBuffer);
				if (ret != rc::OK)
				{
					return ret;
				}
				tempFooter = getIXPageIndexFooter(tempBuffer);
				tempFooter->parent = _rootPageNum;
				memcpy(tempBuffer + PAGE_SIZE - sizeof(IX_PageIndexFooter), tempFooter, sizeof(IX_PageIndexFooter));

				// Write the new page information to disk
				ret = fileHandle.writePage(rightPage, tempBuffer);
				if (ret != rc::OK)
				{
					return ret;
				}

				assert(false);
			}

			// Insert the right child into the parent of the left
			PageNum parent = footer->parent;
			ret = insertIntoNonLeaf(fileHandle, parent, attribute, rightKey, rightRid);
			if (ret != rc::OK && ret != rc::BTREE_INDEX_PAGE_FULL)
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

    // Delete the entry from the index now
    ret = deleteRecord(fileHandle, _indexLeafRecordDescriptor, entryRid);
    if (ret != rc::OK)
    {
        return ret;
    }

    // TODO: Is this page completely empty? We should remove it otherwise it will mess up our scan (I think)

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

	// Create the new entry to add
	IndexNonLeafRecord entry;
	entry.pagePointer = rid;
	entry.nextSlot.pageNum = 0;
	entry.nextSlot.slotNum = 0;
	entry.key = keyData;

	// Compute the record length
	unsigned recLength = 0;
    unsigned recHeaderSize = 0;
    unsigned* recHeader = NULL;
    ret = generateRecordHeader(_indexLeafRecordDescriptor, &entry, recHeader, recLength, recHeaderSize);

	// Determine if we can fit on this page
	unsigned targetFreeSpace = calculateFreespace(footer->core.freeSpaceOffset, footer->core.numSlots);
	if (recLength >= targetFreeSpace)
	{
		return rc::BTREE_INDEX_PAGE_FULL;
	}
	else if (footer->core.numSlots == 0)
	{
		// Insert the new record into the root
		RID newEntry;
		ret = insertRecordToPage(fileHandle, _indexLeafRecordDescriptor, &entry, page, newEntry);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Pull in the updated footer
		ret = fileHandle.readPage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
		footer = getIXPageIndexFooter(pageBuffer);
		
		// Update the header of the page to point to this new entry
		footer->firstRecord = newEntry;
		memcpy(pageBuffer + PAGE_SIZE - sizeof(IX_PageIndexFooter), footer, sizeof(IX_PageIndexFooter));

		// Write the new page information to disk
		ret = fileHandle.writePage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}

		// debug
		// try read
		IndexLeafRecord currEntry;
		ret = readRecord(fileHandle, _indexLeafRecordDescriptor, newEntry, &currEntry);
		assert(currEntry.key.integer == entry.key.integer);
	}
	else // if we can fit, find the spot in the list where the new record will go
	{
		bool found = false;
		bool atEnd = false;
		RID currRid = footer->firstRecord;
		RID prevRid;
		IndexNonLeafRecord currEntry;
		IndexNonLeafRecord prevEntry;

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
			// ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &entry, newEntry);
			// ret = insertRecordInplace(_indexLeafRecordDescriptor, &entry, page, pageBuffer, newEntry);
			ret = insertRecordToPage(fileHandle, _indexLeafRecordDescriptor, &entry, page, newEntry);
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
			ret = insertRecordToPage(fileHandle, _indexLeafRecordDescriptor, &entry, page, newEntry);
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
	if (footer->core.numSlots == 0)
	{
		IndexLeafRecord leaf;
		leaf.dataRid = rid;
		leaf.nextSlot.pageNum = 0;
		leaf.nextSlot.slotNum = 0;
		leaf.key = keyData;

		// Insert the new record into the root
		RID newEntry;
		// ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);
		// ret = insertRecordInplace(_indexLeafRecordDescriptor, &leaf, page, pageBuffer, newEntry);
		ret = insertRecordToPage(fileHandle, _indexLeafRecordDescriptor, &leaf, page, newEntry);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Pull in the updated footer
		ret = fileHandle.readPage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
		footer = getIXPageIndexFooter(pageBuffer);
		
		// Update the header of the page to point to this new entry
		footer->firstRecord = newEntry;
		memcpy(pageBuffer + PAGE_SIZE - sizeof(IX_PageIndexFooter), footer, sizeof(IX_PageIndexFooter));

		// Write the new page information to disk
		ret = fileHandle.writePage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}

		// debug - make sure the changes were made correctly
		// TODO: remove when done
		IndexLeafRecord currEntry;
		ret = readRecord(fileHandle, _indexLeafRecordDescriptor, newEntry, &currEntry);
		assert(currEntry.key.integer == leaf.key.integer);
	}
	else
	{
		IndexLeafRecord leaf;
		leaf.dataRid = rid;
		leaf.nextSlot.pageNum = 0;
		leaf.nextSlot.slotNum = 0;
		leaf.key = keyData;

		// Compute the record length
		unsigned recLength = 0;
	    unsigned recHeaderSize = 0;
	    unsigned* recHeader = NULL;
	    ret = generateRecordHeader(_indexLeafRecordDescriptor, &leaf, recHeader, recLength, recHeaderSize);

		// Determine if we can fit on this page
		unsigned targetFreeSpace = calculateFreespace(footer->core.freeSpaceOffset, footer->core.numSlots);
		if (recLength > targetFreeSpace)
		{
			return rc::BTREE_INDEX_PAGE_FULL;
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
					if (currEntry.nextSlot.pageNum == 0) 
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
				// ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);
				// ret = insertRecordInplace(_indexLeafRecordDescriptor, &leaf, page, pageBuffer, newEntry);
				ret = insertRecordToPage(fileHandle, _indexLeafRecordDescriptor, &leaf, page, newEntry);
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
				// ret = insertRecord(fileHandle, _indexLeafRecordDescriptor, &leaf, newEntry);
				// ret = insertRecordInplace(_indexLeafRecordDescriptor, &leaf, page, pageBuffer, newEntry);
				ret = insertRecordToPage(fileHandle, _indexLeafRecordDescriptor, &leaf, page, newEntry);
				if (ret != rc::OK)
				{
					return ret;
				}

				// ret = fileHandle.writePage(page, pageBuffer);
				// if (ret != rc::OK)
				// {
				// 	return ret;
				// }

				// Update the previous entry to point to the new entry
				prevEntry.nextSlot = newEntry;
				ret = updateRecord(fileHandle, _indexLeafRecordDescriptor, &prevEntry, prevRid);
				if (ret != rc::OK)
				{
					return ret;
				}
			}

			// Write the new page information to disk
			// ret = fileHandle.writePage(page, pageBuffer);
			// if (ret != rc::OK)
			// {
			// 	return ret;
			// }
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
		RC ret = IndexManager::instance()->readRecord(fileHandle, indexNonLeafRecordDescriptor(), currRid, &currEntry);
		if (ret != rc::OK)
		{
			return ret;
		}

		ret = key->compare(attribute.type, currEntry.key, compareResult);
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
		// targetRid.pageNum = footer->rightChild;
		targetRid = prevEntry.pagePointer;
	}

	// Save the result and return OK
	pageNum = targetRid.pageNum;
	return rc::OK;
}

RC IndexManager::findSmallestLeafIndexEntry(FileHandle& fileHandle, RID& rid)
{
	// Begin at the root
	char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(1, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Extract the first record's leftChild
	IX_PageIndexFooter* footer = IndexManager::getIXPageIndexFooter(pageBuffer);
	PageNum leftChild = footer->leftChild;

	// Continue down all leftChild pointers until we hit a leaf
	while(leftChild != 0 && !footer->isLeafPage)
	{
		// Load in the next page
		ret = fileHandle.readPage(leftChild, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}

		leftChild = footer->leftChild;
	}

	// If we didn't find a leaf, our BTree was corrupted
	if (!footer->isLeafPage)
	{
		return rc::BTREE_CANNOT_FIND_LEAF;
	}

	// Get the first (smallest) element from this leaf
	rid = footer->firstRecord;
	if (rid.pageNum == 0)
	{
		return rc::BTREE_INDEX_LEAF_ENTRY_NOT_FOUND;
	}

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
		RC ret = IndexManager::instance()->readRecord(fileHandle, IndexManager::indexLeafRecordDescriptor(), currRid, &currEntry);
		// RC ret = readRecord(fileHandle, IndexManager::instance()->indexLeafRecordDescriptor, currRid, &currEntry);
		if (ret != rc::OK)
		{
			return ret;
		}

		ret = key->compare(attribute.type, currEntry.key, compareResult);
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
		return rc::BTREE_INDEX_LEAF_ENTRY_NOT_FOUND;
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
	assert(targetFooter->core.numSlots > 0); 

	// Allocate the new page and save its reference
	newPageNum = fileHandle.getNumberOfPages();
	ret = newPage(fileHandle, newPageNum, targetFooter->isLeafPage, targetFooter->nextLeafPage, 0);
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
	unsigned numToMove = (targetFooter->core.numSlots / 2);
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

	// Read in the first entry in the second half - this entry now points to the left
	if (targetFooter->isLeafPage)
	{
		IndexLeafRecord lRecord;
		readRecord(fileHandle, _indexLeafRecordDescriptor, currRid, &lRecord);
		currRid = lRecord.nextSlot;
		numToMove--; // make sure we don't skip entries 
	}
	else
	{
		IndexNonLeafRecord nlRecord;	
		readRecord(fileHandle, _indexNonLeafRecordDescriptor, currRid, &nlRecord);
		currRid = nlRecord.nextSlot;
		newFooter->leftChild = currRid.pageNum;
		cout << "updated the left child" << endl;
	}

	// The currRid variable now points to the correct spot in the list from which we should start moving
	// Move the rest over to the new page
	for (unsigned i = numToMove + 1; i < targetFooter->core.numSlots; i++)
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
	return (IX_PageIndexFooter*)getPageIndexFooter(pageBuffer, sizeof(IX_PageIndexFooter));
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

IX_ScanIterator::IX_ScanIterator()
	: _fileHandle(NULL), 
	_attribute(),
	_lowKeyValue(NULL), 
	_highKeyValue(NULL), 
	_lowKeyInclusive(false), 
	_highKeyInclusive(false),
	_currentRecordRid(),
	_beginRecordRid(),
	_endRecordRid(),
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

	RC ret = rc::OK;

	_fileHandle = fileHandle;
	_attribute = attribute;
	_lowKeyInclusive = lowKeyInclusive;
	_highKeyInclusive = highKeyInclusive;

	// Traverse down the left pointers to find the lowest RID
	RID lowestPossibleRid;
	ret = IndexManager::findSmallestLeafIndexEntry(*_fileHandle, lowestPossibleRid);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Copy over the key values to local memory
	if (lowKey)
	{
		ret = Attribute::allocateValue(attribute.type, lowKey, &_lowKeyValue);
		if (ret != rc::OK)
		{
			if (_lowKeyValue)
				free(_lowKeyValue);

			_lowKeyValue = NULL;
			return ret;
		}

		// TODO: Iterate through RIDs until we find something matching the _lowKeyValue
		_beginRecordRid = lowestPossibleRid;
	}
	else
	{
		if (_lowKeyValue)
			free(_lowKeyValue);

		_lowKeyValue = NULL;
		_beginRecordRid = lowestPossibleRid;
	}

	if (highKey)
	{
		RC ret = Attribute::allocateValue(attribute.type, highKey, &_highKeyValue);
		if (ret != rc::OK)
		{
			if (_highKeyValue)
				free(_highKeyValue);

			_highKeyValue = NULL;
			return ret;
		}

		// TODO: Iterate through RIDs until we find something matching _highKeyValue
		_endRecordRid = lowestPossibleRid;
	}
	else
	{
		if (_highKeyValue)
			free(_highKeyValue);

		_highKeyValue = NULL;
		// TODO: Iterate through RIDs until we find the largest
		_endRecordRid = lowestPossibleRid;
	}

	// Start at the beginning RID (keep track of beginRecordRid just for debug purposes for now)
	_currentRecordRid = _beginRecordRid;

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

		// case TypeVarChar:
		// 	memcpy(&size, key, sizeof(unsigned));
		// 	memcpy(&varchar, (char*)key + sizeof(unsigned), size);
		// 	break;

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

		// case TypeVarChar:
		// 	// TODO: Handle "AAA" vs "AAAAA" where only lengths are unequal
		// 	result = strncmp(varchar, that.varchar, size);
		// 	break;

		default:
			return rc::ATTRIBUTE_INVALID_TYPE;
	}

	return rc::OK;
}
