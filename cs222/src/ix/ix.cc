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
	Attribute attr;
	std::vector<Attribute> commonDescriptor;
	attr.name = "nextSlot_PageNum";			attr.type = TypeInt;		attr.length = 4;			commonDescriptor.push_back(attr);
	attr.name = "nextSlot_SlotNum";			attr.type = TypeInt;		attr.length = 4;			commonDescriptor.push_back(attr);
	attr.name = "rid_PageNum";				attr.type = TypeInt;		attr.length = 4;			commonDescriptor.push_back(attr);
	attr.name = "rid_SlotNum";				attr.type = TypeInt;		attr.length = 4;			commonDescriptor.push_back(attr);
	attr.name = "keySize";					attr.type = TypeInt;		attr.length = 4;			commonDescriptor.push_back(attr);

	// Copy the common part of the record descriptor to all types
	_indexIntRecordDescriptor.insert(_indexIntRecordDescriptor.end(), commonDescriptor.begin(), commonDescriptor.end());
	_indexRealRecordDescriptor.insert(_indexRealRecordDescriptor.end(), commonDescriptor.begin(), commonDescriptor.end());
	_indexVarCharRecordDescriptor.insert(_indexVarCharRecordDescriptor.end(), commonDescriptor.begin(), commonDescriptor.end());

	// The key part of the record is the only difference
	attr.name = "key";

	// Integer key records
	attr.type = TypeInt;
	attr.length = 4;
	_indexIntRecordDescriptor.push_back(attr);

	// Real key records
	attr.type = TypeReal;
	attr.length = 4;
	_indexRealRecordDescriptor.push_back(attr);

	// VarChar key records
	attr.type = TypeVarChar;
	attr.length = MAX_KEY_SIZE;
	_indexVarCharRecordDescriptor.push_back(attr);
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
	RETURN_ON_ERR(ret);

	// Open up the file so we can initialize it with some data
	FileHandle fileHandle;
	ret = openFile(fileName, fileHandle);
	RETURN_ON_ERR(ret);

	// Create the root page, mark it as a leaf, it's page 1
	_rootPageNum = 1;
	ret = newPage(fileHandle, _rootPageNum, true, 0, 0);
	RETURN_ON_ERR(ret);

	// We're done, leave the file closed
	ret = closeFile(fileHandle);
	RETURN_ON_ERR(ret);

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
		RETURN_ON_ERR(ret);
	}


	return rc::OK;
}

RC IndexManager::insertEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
	// Pull in the root page
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(_rootPageNum, pageBuffer);
	RETURN_ON_ERR(ret);

	// Build the key struct for the index 
	KeyValueData keyData;
	ret = keyData.init(attribute.type, key);
	RETURN_ON_ERR(ret);

	// Extract the header
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// Traverse down the tree to the leaf, using non-leaves along the way
	PageNum nextPage = _rootPageNum;
	while (footer->isLeafPage == false)
	{
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, &keyData, nextPage);
		RETURN_ON_ERR(ret);

		// Pull the designated page into memory and refresh the footer
		memset(pageBuffer, 0, PAGE_SIZE);
		ret = fileHandle.readPage(nextPage, pageBuffer);
		RETURN_ON_ERR(ret);
		footer = getIXPageIndexFooter(pageBuffer);
	}

	// We're at a leaf now...
	assert(footer->isLeafPage);

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	ret = insertIntoLeaf(fileHandle, nextPage, attribute, keyData, rid);
	if (ret != rc::OK && ret != rc::BTREE_INDEX_PAGE_FULL)
	{
		RETURN_ON_ERR(ret);
		assert(false);
	}
	else
	{
		while (ret == rc::BTREE_INDEX_PAGE_FULL)
		{
			// Split the page (no difference between leaves and non-leaves)
			PageNum parent = footer->parent;
			PageNum leftPage = nextPage;
			PageNum rightPage;
			KeyValueData rightKey;
			RID rightRid;
			ret = split(fileHandle, recordDescriptor, leftPage, rightPage, rightRid, rightKey);
			RETURN_ON_ERR(ret);

			// Check to see if we need to grow by one level, and if so, add a new page with enough space to 
			// hold the new index entry
			if (nextPage == _rootPageNum)
			{
				PageNum oldRoot = _rootPageNum;
				_rootPageNum = fileHandle.getNumberOfPages();
				ret = newPage(fileHandle, _rootPageNum, false, 0, leftPage);
				RETURN_ON_ERR(ret);

				// Update the left/right children parents to point to the new root
				unsigned char tempBuffer[PAGE_SIZE] = {0};
				ret = fileHandle.readPage(leftPage, tempBuffer);
				RETURN_ON_ERR(ret);

				IX_PageIndexFooter* tempFooter = getIXPageIndexFooter(tempBuffer);
				tempFooter->parent = _rootPageNum;

				// Re-update the reference to the new footer to be used in the -final- insertion
				footer = tempFooter;

				// Write the new page information to disk
				ret = fileHandle.writePage(leftPage, tempBuffer);
				RETURN_ON_ERR(ret);

				// Reset the buffer and then read in the right page
				memset(tempBuffer, 0, PAGE_SIZE);
				ret = fileHandle.readPage(rightPage, tempBuffer);
				RETURN_ON_ERR(ret);

				tempFooter->parent = _rootPageNum;
				parent = _rootPageNum;

				// Write the new page information to disk
				ret = fileHandle.writePage(rightPage, tempBuffer);
				RETURN_ON_ERR(ret);

				RC ret = fileHandle.readPage(_rootPageNum, tempBuffer);
				RETURN_ON_ERR(ret);

				// Extract the header
				tempFooter = getIXPageIndexFooter(tempBuffer);
				assert(tempFooter->isLeafPage == false);
				RID firstRid = tempFooter->firstRecord;
				IndexRecord rec;
				readRecord(fileHandle, recordDescriptor, firstRid, &rec);
			}

			// Insert the right child into the parent of the left
			ret = insertIntoNonLeaf(fileHandle, parent, attribute, rightKey, rightRid);
			if (ret != rc::OK && ret != rc::BTREE_INDEX_PAGE_FULL)
			{
				assert(false);
				RETURN_ON_ERR(ret);
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
	RETURN_ON_ERR(ret);

	// Build the key struct for the index 
	KeyValueData keyData;
	ret = keyData.init(attribute.type, key);
	RETURN_ON_ERR(ret);

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	// Extract the header
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// Traverse down the tree to the leaf, using non-leaves along the way
	PageNum nextPage;
	while (footer->isLeafPage == false)
	{
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, &keyData, nextPage);
		RETURN_ON_ERR(ret);

		// Pull the designated page into memory
		memset(pageBuffer, 0, PAGE_SIZE);
		ret = fileHandle.readPage(nextPage, pageBuffer);
		RETURN_ON_ERR(ret);

		// Update the header
		footer = getIXPageIndexFooter(pageBuffer);
	}

	// Now search along the leaf page
	assert(footer->isLeafPage);
	RID entryRid, prevEntryRid, nextEntryRid, dataRid;
	ret = findLeafIndexEntry(fileHandle, footer, attribute, &keyData, entryRid, prevEntryRid, nextEntryRid, dataRid);
	RETURN_ON_ERR(ret);

	// If we delete the first RID on the page, be sure to update the footer pointer to the "new" first RID
	IndexRecord record;
	if (entryRid.slotNum == footer->firstRecord.slotNum)
	{
		ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, footer->firstRecord, &record);
		RETURN_ON_ERR(ret);
		footer->firstRecord = record.nextSlot;
		memcpy(pageBuffer + PAGE_SIZE - sizeof(IX_PageIndexFooter), footer, sizeof(IX_PageIndexFooter));
		ret = fileHandle.writePage(nextPage, pageBuffer);
		RETURN_ON_ERR(ret);
	}

	// Update the next pointer of our previous record if it exists
	if (prevEntryRid.pageNum != 0)
	{
		ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, prevEntryRid, &record);
		RETURN_ON_ERR(ret);

		record.nextSlot = nextEntryRid;

		ret = IndexManager::instance()->updateRecord(fileHandle, recordDescriptor, &record, prevEntryRid);
		RETURN_ON_ERR(ret);
	}

    // Delete the entry from the index now
    ret = deleteRecord(fileHandle, recordDescriptor, entryRid);
    RETURN_ON_ERR(ret);

    // TODO: Is this page completely empty? We should remove it otherwise it will mess up our scan (I think)
    // Yes, we'll need to do that... not sure what the best way is though... :-(

    return rc::OK;
}

RC IndexManager::insertIntoNonLeaf(FileHandle& fileHandle, PageNum& page, const Attribute &attribute, KeyValueData keyData, RID rid)
{
	// Pull in the root page and then extract the header
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(page, pageBuffer);
	RETURN_ON_ERR(ret);

	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// Create the new entry to add
	IndexRecord entry;
	entry.rid = rid;
	entry.nextSlot.pageNum = 0;
	entry.nextSlot.slotNum = 0;
	entry.key = keyData;

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	// Compute the record length
	unsigned recLength = 0;
    unsigned recHeaderSize = 0;
    unsigned* recHeader = NULL;
    ret = generateRecordHeader(recordDescriptor, &entry, recHeader, recLength, recHeaderSize);

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
		ret = insertRecordToPage(fileHandle, recordDescriptor, &entry, page, newEntry);
		RETURN_ON_ERR(ret);

		// Pull in the updated footer
		ret = fileHandle.readPage(page, pageBuffer);
		RETURN_ON_ERR(ret);

		footer = getIXPageIndexFooter(pageBuffer);
		
		// Update the header of the page to point to this new entry
		footer->firstRecord = newEntry;
		memcpy(pageBuffer + PAGE_SIZE - sizeof(IX_PageIndexFooter), footer, sizeof(IX_PageIndexFooter));

		// Write the new page information to disk
		ret = fileHandle.writePage(page, pageBuffer);
		RETURN_ON_ERR(ret);

		// debug
		// try read
		IndexRecord currEntry;
		ret = readRecord(fileHandle, recordDescriptor, newEntry, &currEntry);
		assert(currEntry.key.integer == entry.key.integer);
	}
	else // if we can fit, find the spot in the list where the new record will go
	{
		bool found = false;
		bool atEnd = false;
		RID currRid = footer->firstRecord;
		RID prevRid;
		IndexRecord currEntry;
		IndexRecord prevEntry;
		// RID prevRid;
		RID targetPrevRid;
		RID targetRid;
		IndexRecord targetPrevEntry;
		IndexRecord targetEntry;
		// IndexRecord currEntry;
		// IndexRecord prevEntry;

		int index = 0;
		int target = 0;
		while (currRid.pageNum > 0) // second condition implies the end of the chain
		{
			// Pull in the next entry
			ret = readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
			RETURN_ON_ERR(ret);

			int compareResult = 0;
			ret = keyData.compare(attribute.type, currEntry.key, compareResult);
			RETURN_ON_ERR(ret);

			if (compareResult >= 0)
			{
				target = index;
				target = index;
				targetPrevRid = prevRid;
				targetRid = currRid;
				targetEntry = currEntry;
				targetPrevEntry = prevEntry;
			}
			index++;
			
			// prevRid = targetRid;
			// prevEntry = currEntry;
			// currRid = currEntry.nextSlot;
            
            // Update for next iteration
            prevRid = currRid;
            prevEntry = currEntry;
            currRid = currEntry.nextSlot;

   //          if (compareResult >= 0)
			// {
			// 	found = true;
			// }

   //          // update for next iteration
   //          prevRid = currRid;
   //          prevEntry = currEntry;
   //          currRid = currEntry.nextSlot;

   //          // Check to see if we reached the end of the list
			// if (!found)
			// {
   //              atEnd = true;
   //              found = true;
			// }
		}

		// Drop the record into the right spot in the on-page list
		atEnd = (target + 1) == footer->core.numSlots;
		if (!atEnd) // start or middle of list
		{
			// Insert the new record into the root, and make it point to the current RID
			entry.nextSlot = targetRid;
			RID newEntry;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &entry, page, newEntry);
			RETURN_ON_ERR(ret);

			// Update the previous entry to point to the new entry (it's in the middle now)
			targetPrevEntry.nextSlot = newEntry;
			ret = updateRecord(fileHandle, recordDescriptor, &targetPrevEntry, targetPrevRid);
			RETURN_ON_ERR(ret);
		}
		else // append the RID to the end of the on-page list 
		{
			// Insert the new record into the root
			RID newEntry;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &entry, page, newEntry);
			RETURN_ON_ERR(ret);

			// Update the previous entry to point to the new entry
			targetEntry.nextSlot = newEntry;
			ret = updateRecord(fileHandle, recordDescriptor, &targetEntry, targetRid);
			RETURN_ON_ERR(ret);
		}

		// Write the new page information to disk
		// ret = fileHandle.writePage(page, pageBuffer);
		// RETURN_ON_ERR(ret);
	}

	return rc::OK;
}

RC IndexManager::insertIntoLeaf(FileHandle& fileHandle, PageNum& page, const Attribute &attribute, KeyValueData keyData, RID rid)
{
	// Pull in the page and then extract the header
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(page, pageBuffer);
	RETURN_ON_ERR(ret);

	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	// Special case if the leaf is empty
	if (footer->core.numSlots == 0)
	{
		IndexRecord leaf;
		leaf.rid = rid;
		leaf.nextSlot.pageNum = 0;
		leaf.nextSlot.slotNum = 0;
		leaf.key = keyData;

		// Insert the new record into the root
		RID newEntry;
		ret = insertRecordToPage(fileHandle, recordDescriptor, &leaf, page, newEntry);
		RETURN_ON_ERR(ret);

		// Pull in the updated footer
		ret = fileHandle.readPage(page, pageBuffer);
		RETURN_ON_ERR(ret);

		footer = getIXPageIndexFooter(pageBuffer);
		
		// Update the header of the page to point to this new entry
		footer->firstRecord = newEntry;
		memcpy(pageBuffer + PAGE_SIZE - sizeof(IX_PageIndexFooter), footer, sizeof(IX_PageIndexFooter));

		// Write the new page information to disk
		ret = fileHandle.writePage(page, pageBuffer);
		RETURN_ON_ERR(ret);

		// debug - make sure the changes were made correctly
		// TODO: remove when done
		IndexRecord currEntry;
		ret = readRecord(fileHandle, recordDescriptor, newEntry, &currEntry);
		assert(currEntry.key.integer == leaf.key.integer);
	}
	else
	{
		IndexRecord leaf;
		leaf.rid = rid;
		leaf.nextSlot.pageNum = 0;
		leaf.nextSlot.slotNum = 0;
		leaf.key = keyData;

		// Compute the record length
		unsigned recLength = 0;
	    unsigned recHeaderSize = 0;
	    unsigned* recHeader = NULL;
	    ret = generateRecordHeader(recordDescriptor, &leaf, recHeader, recLength, recHeaderSize);

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
			RID targetPrevRid;
			RID targetRid;
			IndexRecord targetPrevEntry;
			IndexRecord targetEntry;
			IndexRecord currEntry;
			IndexRecord prevEntry;

			int index = 0;
			int target = 0;
			while (currRid.pageNum > 0) // second condition implies the end of the chain
			{
				// Pull in the next entry
				ret = readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
				RETURN_ON_ERR(ret);

				int compareResult = 0;
				ret = keyData.compare(attribute.type, currEntry.key, compareResult);
				RETURN_ON_ERR(ret);

                if (compareResult >= 0)
				{
					target = index;
					targetPrevRid = prevRid;
					targetRid = currRid;
					targetEntry = currEntry;
					targetPrevEntry = prevEntry;
				}
				index++;
				
				// prevRid = targetRid;
				// prevEntry = currEntry;
				// currRid = currEntry.nextSlot;
                
                // Update for next iteration
                prevRid = currRid;
                prevEntry = currEntry;
                currRid = currEntry.nextSlot;

    //             // Check to see if we reached the end of the list
    //             if (currEntry.nextSlot.pageNum == 0)
				// {
    //                 atEnd = true;
    //                 found = true;
				// }
			}

			// Drop the record into the right spot in the on-page list
			atEnd = (target + 1) == footer->core.numSlots;
			if (!atEnd) // start or middle of list
			{
				// Insert the new record into the root, and make it point to the current RID
				leaf.nextSlot = targetRid;
				RID newEntry;
				ret = insertRecordToPage(fileHandle, recordDescriptor, &leaf, page, newEntry);
				RETURN_ON_ERR(ret);

				// Update the previous entry to point to the new entry (it's in the middle now)
				targetPrevEntry.nextSlot = newEntry;
				ret = updateRecord(fileHandle, recordDescriptor, &targetPrevEntry, targetPrevRid);
				RETURN_ON_ERR(ret);
			}
			else // append the RID to the end of the on-page list 
			{
				// Insert the new record into the root
				RID newEntry;
				ret = insertRecordToPage(fileHandle, recordDescriptor, &leaf, page, newEntry);
				RETURN_ON_ERR(ret);

				// ret = fileHandle.writePage(page, pageBuffer);
				// RETURN_ON_ERR(ret);

				// Update the previous entry to point to the new entry
				targetEntry.nextSlot = newEntry;
				ret = updateRecord(fileHandle, recordDescriptor, &targetEntry, targetRid);
				RETURN_ON_ERR(ret);
			}

			// Write the new page information to disk
			// ret = fileHandle.writePage(page, pageBuffer);
			// RETURN_ON_ERR(ret);
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
	IndexRecord currEntry;
	IndexRecord prevEntry;

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	if (footer->core.numSlots == 1)
	{
		RC ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
		RETURN_ON_ERR(ret);

		ret = key->compare(attribute.type, currEntry.key, compareResult);
		RETURN_ON_ERR(ret);

		if (compareResult >= 0)
		{
			pageNum = currEntry.rid.pageNum;
		}
		else
		{
			pageNum = footer->leftChild;
		}
	}
	else
	{
		// Traverse the list of index entries on this non-leaf page
		int index = 0;
		int target = 0;
		while (currRid.pageNum > 0)
		{
			// Pull in the new entry and perform the comparison
			RC ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
			RETURN_ON_ERR(ret);

			ret = key->compare(attribute.type, currEntry.key, compareResult);
			RETURN_ON_ERR(ret);

			if (compareResult >= 0)
			{
				target = index;
				targetRid = currEntry.rid;
			}
			index++;
			
			prevRid = targetRid;
			prevEntry = currEntry;
			currRid = currEntry.nextSlot;
		}

		if (target == 0)
		{
			pageNum = footer->leftChild;
		}
		else
		{
			// Check to see if we traverse the list entirely, in which case the last index entry 
			// contains the page pointer to which we point
			if (currRid.pageNum == 0)
			{
				// targetRid.pageNum = footer->rightChild;
				targetRid = prevEntry.rid;
			}

			// Save the result and return OK
			pageNum = targetRid.pageNum;
		}
	}

	return rc::OK;
}

RC IndexManager::findLargestLeafIndexEntry(FileHandle& fileHandle, RID& rid)
{
    // Begin at the root
    char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(1, pageBuffer);
    RETURN_ON_ERR(ret);

    IX_PageIndexFooter* footer = IndexManager::getIXPageIndexFooter(pageBuffer);

    // Traverse down to the rightmost leaf
    while(!footer->isLeafPage)
    {
        // Iterate through records to find the last one on this page

    }

	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IndexManager::findSmallestLeafIndexEntry(FileHandle& fileHandle, RID& rid)
{
	// Begin at the root
	char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(1, pageBuffer);
	RETURN_ON_ERR(ret);

	// Extract the first record's leftChild
	IX_PageIndexFooter* footer = IndexManager::getIXPageIndexFooter(pageBuffer);
	PageNum leftChild = footer->leftChild;

	// Continue down all leftChild pointers until we hit a leaf
	while(leftChild != 0 && !footer->isLeafPage)
	{
		// Load in the next page
		ret = fileHandle.readPage(leftChild, pageBuffer);
		RETURN_ON_ERR(ret);

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

RC IndexManager::findLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, RID& entryRid, RID& dataRid)
{
	RID nextEntryRid;
	RID prevEntryRid;
	return findLeafIndexEntry(fileHandle, footer, attribute, key, entryRid, prevEntryRid, nextEntryRid, dataRid);
}

RC IndexManager::findLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, RID& entryRid, RID& prevEntryRid, RID& nextEntryRid, RID& dataRid)
{
	assert(&entryRid != &prevEntryRid);
	assert(&entryRid != &nextEntryRid);
	assert(&nextEntryRid != &prevEntryRid);

	int compareResult = -1;

	// zero out RID data 
	entryRid.pageNum = entryRid.slotNum = 0;
	prevEntryRid.pageNum = prevEntryRid.slotNum = 0;
	nextEntryRid.pageNum = nextEntryRid.slotNum = 0;
	dataRid.pageNum = dataRid.slotNum = 0;

	// Extract the first record
	RID currRid = footer->firstRecord;
	IndexRecord currEntry;

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	// Traverse the list of index entries on this leaf page
	while (currRid.pageNum > 0)
	{
		// Pull in the new entry and perform the comparison
		RC ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
		RETURN_ON_ERR(ret);

		ret = key->compare(attribute.type, currEntry.key, compareResult);
		RETURN_ON_ERR(ret);

		// If we have a match, save it and return OK, otherwise keep traversing the entries on the leaf page
		if (compareResult == 0)
		{
			entryRid = currRid;
			dataRid = currEntry.rid;
			break;
		}
		else
		{
			prevEntryRid = currRid;
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
		// Pull in the next RID after our current one
		nextEntryRid = currEntry.nextSlot;

		return rc::OK;
	}
}

RC IndexManager::split(FileHandle& fileHandle, const std::vector<Attribute>& recordDescriptor, PageNum& targetPageNum, PageNum& newPageNum, RID& rightRid, KeyValueData& rightKey)
{
	// Read in the page to be split
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(targetPageNum, pageBuffer);
	RETURN_ON_ERR(ret);

	// Extract the header
	IX_PageIndexFooter* targetFooter = getIXPageIndexFooter(pageBuffer);
	assert(targetFooter->core.numSlots > 0); 

	// Allocate the new page and save its reference
	newPageNum = fileHandle.getNumberOfPages();
	ret = newPage(fileHandle, newPageNum, targetFooter->isLeafPage, targetFooter->nextLeafPage, 0);
	RETURN_ON_ERR(ret);

	// Update the nextLeaf pointer if needed
	if (targetFooter->isLeafPage)
	{
		targetFooter->nextLeafPage = newPageNum;
	}

	// Save the parent of the new footer
	unsigned char newPageBuffer[PAGE_SIZE] = {0};
	ret = fileHandle.readPage(newPageNum, newPageBuffer);
	RETURN_ON_ERR(ret);

	IX_PageIndexFooter* newFooter = getIXPageIndexFooter(newPageBuffer);
	rightRid.pageNum = newPageNum;
	assert(newFooter->isLeafPage == targetFooter->isLeafPage);
	newFooter->parent = targetFooter->parent;

	// Write the new page information to disk
	ret = fileHandle.writePage(newPageNum, newPageBuffer);
	RETURN_ON_ERR(ret);

	// Read in half of the entries from the target page and insert them onto the new page
	RID currRid = targetFooter->firstRecord;
	unsigned numToMove = (targetFooter->core.numSlots / 2);
	for (unsigned i = 0; i < numToMove; i++)
	{
		// Skip over the first 'numToMove' RIDs since they stay in place
		if (targetFooter->isLeafPage)
		{
			IndexRecord lRecord;
			readRecord(fileHandle, recordDescriptor, currRid, &lRecord);
			currRid = lRecord.nextSlot;
		}
		else
		{
			IndexRecord nlRecord;	
			readRecord(fileHandle, recordDescriptor, currRid, &nlRecord);
			currRid = nlRecord.nextSlot;
		}	
	}

	// Save a reference to the split spot so the disconnect can be made later
	RID splitRid = currRid;

	// Read in the first entry in the second half - this entry now points to the left
	if (targetFooter->isLeafPage)
	{
		IndexRecord lRecord;
		readRecord(fileHandle, recordDescriptor, currRid, &lRecord);
		currRid = lRecord.nextSlot;
		numToMove--; // make sure we don't skip entries 
	}
	else
	{
		IndexRecord nlRecord;	
		readRecord(fileHandle, recordDescriptor, currRid, &nlRecord);
		currRid = nlRecord.nextSlot;
		newFooter->leftChild = currRid.pageNum;
	}

	RID deleteStart = currRid;

	// The currRid variable now points to the correct spot in the list from which we should start moving
	// Move the rest over to the new page
	for (unsigned i = numToMove + 1; i < targetFooter->core.numSlots; i++)
	{
		// Move the entry over to the new page
		RID newEntry;
		if (targetFooter->isLeafPage)
		{
			IndexRecord lRecord;
			readRecord(fileHandle, recordDescriptor, currRid, &lRecord);
			insertRecord(fileHandle, recordDescriptor, &lRecord, newEntry);
			currRid = lRecord.nextSlot;

			// Save the new key
			if (i == (numToMove + 1))
			{
				// rightRid = newEntry;
				rightKey = lRecord.key;
			}
		}
		else
		{
			IndexRecord nlRecord;	
			readRecord(fileHandle, recordDescriptor, currRid, &nlRecord);
			insertRecord(fileHandle, recordDescriptor, &nlRecord, newEntry);
			currRid = nlRecord.nextSlot;

			// Save the new key
			if (i == numToMove)
			{
				// rightRid = newEntry;
				rightKey = nlRecord.key;
			}
		}
	}

	// Delete the ones that were moved...
	// TODO: can this be done above?
	// currRid = deleteStart;
	// for (unsigned i = numToMove + 1; i < targetFooter->core.numSlots; i++)
	// {
	// 	// Skip over the first 'numToMove' RIDs since they stay in place
	// 	if (targetFooter->isLeafPage)
	// 	{
	// 		IndexRecord lRecord;
	// 		readRecord(fileHandle, recordDescriptor, currRid, &lRecord);
	// 		ret = deleteRecord(fileHandle, recordDescriptor, currRid);
	// 		currRid = lRecord.nextSlot;
	// 	}
	// 	else
	// 	{
	// 		IndexRecord nlRecord;
	// 		readRecord(fileHandle, recordDescriptor, currRid, &nlRecord);
	// 		ret = deleteRecord(fileHandle, recordDescriptor, currRid);
	// 		currRid = nlRecord.nextSlot;
	// 	}
	// }

	// Make the disconnect by terminating the on-page list on the first (source) page
	if (targetFooter->isLeafPage)
	{
		IndexRecord lRecord;
		readRecord(fileHandle, recordDescriptor, splitRid, &lRecord);
		lRecord.nextSlot.pageNum = 0;
		lRecord.nextSlot.slotNum = 0;
		ret = updateRecord(fileHandle, recordDescriptor, &lRecord, splitRid);
		RETURN_ON_ERR(ret);
	}
	else
	{
		IndexRecord nlRecord;	
		readRecord(fileHandle, recordDescriptor, splitRid, &nlRecord);
		nlRecord.nextSlot.pageNum = 0;
		nlRecord.nextSlot.slotNum = 0;
		ret = updateRecord(fileHandle, recordDescriptor, &nlRecord, splitRid);
		RETURN_ON_ERR(ret);
	}

	// Read in the page to be split
	ret = fileHandle.readPage(targetPageNum, pageBuffer);
	RETURN_ON_ERR(ret);

	// Extract the header
	targetFooter = getIXPageIndexFooter(pageBuffer);
	assert(targetFooter->core.numSlots > 0); 

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

const std::vector<Attribute>& IndexManager::getIndexRecordDescriptor(AttrType type)
{
	switch(type)
	{
	case TypeInt:		return instance()->_indexIntRecordDescriptor;
	case TypeReal:		return instance()->_indexRealRecordDescriptor;
	case TypeVarChar:	return instance()->_indexVarCharRecordDescriptor;

	default:			assert(false); return instance()->_indexIntRecordDescriptor;
	}
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
	if (!fileHandle || !fileHandle->hasFile())
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
	RETURN_ON_ERR(ret);

	// Copy over the key values to local memory
	if (lowKey)
	{
		ret = Attribute::allocateValue(attribute.type, lowKey, &_lowKeyValue);
		if (ret != rc::OK)
		{
			if (_lowKeyValue)
				free(_lowKeyValue);

			_lowKeyValue = NULL;

			RETURN_ON_ERR(ret);
			assert(false);
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
			
			RETURN_ON_ERR(ret);
			assert(false);
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

		case TypeVarChar:
			memcpy(&size, key, sizeof(unsigned));
			memcpy(varchar, key, size + sizeof(unsigned));
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
			result = strncmp(varchar, that.varchar, size);
			if (result == 0)
			{
				if (size < that.size)
					return -1;

				if (size > that.size)
					return 1;

				return 0;
			}
			break;

		default:
			return rc::ATTRIBUTE_INVALID_TYPE;
	}

	return rc::OK;
}
