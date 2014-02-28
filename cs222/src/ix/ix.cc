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
	: RecordBasedCoreManager(sizeof(IX_PageIndexFooter))
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

RC IndexManager::readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC IndexManager::reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber)
{
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(pageNumber, pageBuffer);
    RETURN_ON_ERR(ret);

	return reorganizeBufferedPage(fileHandle, sizeof(IX_PageIndexFooter), recordDescriptor, pageNumber, pageBuffer);
}

RC IndexManager::createFile(const string &fileName)
{
	RC ret = PagedFileManager::instance()->createFile(fileName.c_str());
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
	ret = newPage(fileHandle, 1, true, 0, 0);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Store the root page to the reserved page 0
	ret = updateRootPage(fileHandle, 1);
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

RC IndexManager::updateRootPage(FileHandle& fileHandle, unsigned newRootPage)
{
	// Read in the reserved page
	unsigned char pageBuffer[PAGE_SIZE];
	RC ret = fileHandle.readPage(0, pageBuffer);
	RETURN_ON_ERR(ret);

	// Place the root page data at the very end of the page
	unsigned* rootPage = (unsigned*)( (char*)pageBuffer + PAGE_SIZE - sizeof(unsigned) );
	*rootPage = newRootPage;

	// Write the page back
	ret = fileHandle.writePage(0, pageBuffer);
	RETURN_ON_ERR(ret);

	return rc::OK;
}

RC IndexManager::newPage(FileHandle& fileHandle, PageNum pageNum, bool isLeaf, PageNum nextLeafPage, PageNum leftChild)
{
	const unsigned currentNumPages = fileHandle.getNumberOfPages();
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
	footerTemplate.leftChild = leftChild;
	footerTemplate.freespaceList = NUM_FREESPACE_LISTS - 1;

	unsigned char pageBuffer[PAGE_SIZE];
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	// If we are 'new'ing a previously allocated page, just zero out the data there
	if (pageNum < currentNumPages)
	{
		memset(pageBuffer, 0, PAGE_SIZE);
		memcpy(footer, &footerTemplate, sizeof(footerTemplate));

		RC ret = fileHandle.writePage(pageNum, pageBuffer);
		RETURN_ON_ERR(ret);
	}
	else
	{
		// TODO: Check freespace list to see if there's a completely empty page available
		// Append as many pages as needed (should be only 1)
		unsigned requiredPages = pageNum - currentNumPages + 1;
		for (unsigned i = 0; i < requiredPages; i++)
		{
			memset(pageBuffer, 0, PAGE_SIZE);
			memcpy(footer, &footerTemplate, sizeof(footerTemplate));

			RC ret = fileHandle.appendPage(pageBuffer);
			RETURN_ON_ERR(ret);
		}
	}

	return rc::OK;
}

RC IndexManager::insertEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
	// Pull in the root page
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = readRootPage(fileHandle, pageBuffer);
	RETURN_ON_ERR(ret);

	// Build the key struct for the index 
	KeyValueData keyData;
	ret = keyData.init(attribute.type, key);
	RETURN_ON_ERR(ret);

	// Extract the header
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);
	const PageNum rootPage = footer->pageNumber;

	// Traverse down the tree to the leaf, using non-leaves along the way
	PageNum insertDestination = rootPage;
	while (footer->isLeafPage == false)
	{
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, &keyData, insertDestination);
		RETURN_ON_ERR(ret);

		// Pull the designated page into memory and refresh the footer
		memset(pageBuffer, 0, PAGE_SIZE);
		ret = fileHandle.readPage(insertDestination, pageBuffer);
		RETURN_ON_ERR(ret);
	}

	// We're at a leaf now...
	assert(footer->isLeafPage);

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	cout << "Insert entry going into leaf: " << insertDestination << endl;
	ret = insertIntoLeaf(fileHandle, insertDestination, attribute, keyData, rid);
	if (ret != rc::OK && ret != rc::BTREE_INDEX_PAGE_FULL)
	{
		RETURN_ON_ERR(ret);
		assert(false);
	}
	else
	{
		bool needsToInsert = ret != rc::OK;
		PageNum nextPage = insertDestination;
		insertDestination = 0;

		// TODO: Verify cascading works
		while (ret == rc::BTREE_INDEX_PAGE_FULL)
		{
			// Split the page (no difference between leaves and non-leaves)
			PageNum parent = footer->parent;
			PageNum leftPage = nextPage;
			PageNum rightPage;
			KeyValueData rightKey;
			RID rightRid;

			// printIndex(fileHandle, attribute, true);

			ret = deletelessSplit(fileHandle, recordDescriptor, leftPage, rightPage, rightRid, rightKey);
			// ret = split(fileHandle, recordDescriptor, leftPage, rightPage, rightRid, rightKey);
			RETURN_ON_ERR(ret);

			// Our first split will tell us where to put or original value
			if (insertDestination == 0)
			{
				insertDestination = rightRid.pageNum;
			}

			// Check to see if we need to grow by one level, and if so, add a new page with enough space to 
			// hold the new index entry
			if (nextPage == rootPage)
			{
				PageNum oldRoot = rootPage;
				PageNum newRootPage = fileHandle.getNumberOfPages();
				ret = updateRootPage(fileHandle, newRootPage);
				RETURN_ON_ERR(ret);

				ret = newPage(fileHandle, newRootPage, false, 0, leftPage);
				RETURN_ON_ERR(ret);

				cout << "\t\t====>ROOT GREW: " << newRootPage << endl;

				// Update the left/right children parents to point to the new root
				unsigned char tempBuffer[PAGE_SIZE] = {0};
				ret = fileHandle.readPage(leftPage, tempBuffer);
				RETURN_ON_ERR(ret);

				IX_PageIndexFooter* tempFooter = getIXPageIndexFooter(tempBuffer);
				tempFooter->parent = newRootPage;

				// Write the new page information to disk
				ret = fileHandle.writePage(leftPage, tempBuffer);
				RETURN_ON_ERR(ret);

				// Reset the buffer and then read in the right page
				memset(tempBuffer, 0, PAGE_SIZE);
				ret = fileHandle.readPage(rightPage, tempBuffer);
				RETURN_ON_ERR(ret);

				tempFooter->parent = newRootPage;
				parent = newRootPage;

				// Write the new page information to disk
				ret = fileHandle.writePage(rightPage, tempBuffer);
				RETURN_ON_ERR(ret);

				// Read the new non-leaf page back in
				memset(tempBuffer, 0, PAGE_SIZE);
				RC ret = fileHandle.readPage(newRootPage, tempBuffer);
				RETURN_ON_ERR(ret);

				tempFooter = getIXPageIndexFooter(tempBuffer);
				assert(tempFooter->isLeafPage == false);

				// Update the footer to point left to the left page
				tempFooter->leftChild = leftPage;
			}

			// Insert the right child into the parent of the left
			ret = insertIntoNonLeaf(fileHandle, parent, attribute, rightKey, rightRid);
			if (ret != rc::OK && ret != rc::BTREE_INDEX_PAGE_FULL)
			{
				assert(false);
				RETURN_ON_ERR(ret);
			}

			if (ret == rc::BTREE_INDEX_PAGE_FULL)
			{
				// assert(false);
			}
			nextPage = parent;
		}

		// Don't forget to insert the actual item we were trying to insert in the first place!
		if (needsToInsert)
		{
			ret = insertIntoLeaf(fileHandle, insertDestination, attribute, keyData, rid);
			RETURN_ON_ERR(ret);
		}
	}
	
	return rc::OK;
}

RC IndexManager::deleteEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid)
{
    // Pull in the root page
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = readRootPage(fileHandle, pageBuffer);
	RETURN_ON_ERR(ret);

	// Extract the header
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);
	const PageNum rootPageNum = footer->pageNumber;

	// Build the key struct for the index 
	KeyValueData keyData;
	ret = keyData.init(attribute.type, key);
	RETURN_ON_ERR(ret);

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	// Traverse down the tree to the leaf, using non-leaves along the way
	PageNum nextPage = rootPageNum;
	while (footer->isLeafPage == false)
	{
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, &keyData, nextPage);
		RETURN_ON_ERR(ret);

		// Pull the designated page into memory
		memset(pageBuffer, 0, PAGE_SIZE);
		ret = fileHandle.readPage(nextPage, pageBuffer);
		RETURN_ON_ERR(ret);
	}

	// Now search along the leaf page
	assert(footer->isLeafPage);
	RID entryRid, prevEntryRid, nextEntryRid, dataRid;
	ret = findLeafIndexEntry(fileHandle, footer, attribute, &keyData, entryRid, prevEntryRid, nextEntryRid, dataRid);
	if (ret != rc::OK)
	{
		return ret;
	}

	// printIndex(fileHandle, attribute, true);
	cout << "Leaf entry RID: " << entryRid.pageNum << "," << entryRid.slotNum << endl;

	// If we delete the first RID on the page, be sure to update the footer pointer to the "new" first RID
	IndexRecord record;
	if (entryRid.slotNum == footer->firstRecord.slotNum)
	{
		ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, footer->firstRecord, &record);
		RETURN_ON_ERR(ret);
		footer->firstRecord = record.nextSlot;
		while (footer->firstRecord.pageNum != 0) 
		{
			ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, footer->firstRecord, &record);
			RETURN_ON_ERR(ret);
			footer->firstRecord = record.nextSlot;
		}
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

		cout << "Update prev pointer on deletion: " << entryRid.pageNum << endl;
		ret = IndexManager::instance()->updateRecord(fileHandle, recordDescriptor, &record, prevEntryRid);
		RETURN_ON_ERR(ret);
	}

    // Delete the entry from the index now
    ret = deleteRecord(fileHandle, recordDescriptor, entryRid);
    RETURN_ON_ERR(ret);

	// Check if the page has become completely empty
	ret = fileHandle.readPage(entryRid.pageNum, pageBuffer);
	RETURN_ON_ERR(ret);

	PageNum curPage = footer->parent;
	while (footer->numSlots == 0 && curPage > 1)
	{
		// We need to remove the this from our parent
		ret = fileHandle.readPage(curPage, pageBuffer);
		RETURN_ON_ERR(ret);

		// Search for the record
		RID targetRid, prevRid;
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, &keyData, targetRid, prevRid);
		RETURN_ON_ERR(ret);

		// Read in the target record so we can see what the next record is
		ret = readRecord(fileHandle, recordDescriptor, targetRid, &record, pageBuffer);
		RETURN_ON_ERR(ret);

		const RID nextRid = record.nextSlot;

		// TODO: Deal with the case where we're trying to delete the 'leftChild' pointer

		assert(targetRid.pageNum == footer->pageNumber);
		if(prevRid.pageNum == 0)
		{
			// If we don't have a previous RID, this should be the first record, so we need to update that
			assert(footer->firstRecord.slotNum == targetRid.slotNum);

			// Update the firstRecord data in the footer
			footer->firstRecord = nextRid;
		}
		else
		{
			// We need read in the target record to see what the nextSlot is
			ret = readRecord(fileHandle, recordDescriptor, prevRid, &record, pageBuffer);
			RETURN_ON_ERR(ret);

			record.nextSlot = nextRid;
			
			// Update the record
			ret = updateRecordInplace(fileHandle, recordDescriptor, &record, prevRid, pageBuffer);
			RETURN_ON_ERR(ret);
		}

		// Delete the record
		ret = deleteRecordInplace(fileHandle, recordDescriptor, targetRid, pageBuffer);
		RETURN_ON_ERR(ret);

		// Write out the update non-leaf page
		fileHandle.writePage(curPage, pageBuffer);
	}

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
	unsigned targetFreeSpace = calculateFreespace(footer->freeSpaceOffset, footer->numSlots);
	if ((recLength + sizeof(PageIndexSlot)) >= targetFreeSpace)
	{
		if (footer->numSlots == 0)
			return rc::BTREE_KEY_TOO_LARGE;

		return rc::BTREE_INDEX_PAGE_FULL;
	}

	if (footer->numSlots == 0)
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
		cout << "Updating first record of " << page << " to " << newEntry.pageNum << "," << newEntry.slotNum << endl;
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
		RID targetPrevRid;
		RID targetRid = footer->firstRecord;
		IndexRecord targetPrevEntry;
		IndexRecord targetEntry;

		// printIndex(fileHandle, attribute, true);

		int numEntries = 0;
		int target = 0;
		bool foundMatch = false;
		while (currRid.pageNum > 0) // second condition implies the end of the chain
		{
			// Pull in the next entry
			// cout << "Reading: " << currRid.pageNum << "," << currRid.slotNum << endl;
			ret = readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
			RETURN_ON_ERR(ret);

			int compareResult = 0;
			ret = keyData.compare(attribute.type, currEntry.key, compareResult);
			RETURN_ON_ERR(ret);

			if (compareResult >= 0)
			{
				foundMatch = true;
				target = numEntries;
				targetPrevRid = prevRid;
				targetRid = currRid;
				targetEntry = currEntry;
				targetPrevEntry = prevEntry;
			}
			numEntries++;

            // Update for next iteration
            prevRid = currRid;
            prevEntry = currEntry;
            currRid = currEntry.nextSlot;
		}

		// Drop the record into the right spot in the on-page list
		atEnd = (target + 1) == numEntries;
		if (!atEnd && targetPrevRid.pageNum > 0 && numEntries > 1) // middle of list
		{
			// Insert the new record into the root, and make it point to the current RID
			entry.nextSlot = targetRid;
			RID newEntry;
			cout << "Inserting to middle of page: " << page << endl;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &entry, page, newEntry);
			RETURN_ON_ERR(ret);

			// Update the previous entry to point to the new entry (it's in the middle now)
			targetPrevEntry.nextSlot = newEntry;
			cout << "Updating middle: " << targetPrevRid.pageNum << "," << targetPrevRid.slotNum << endl;
			ret = updateRecord(fileHandle, recordDescriptor, &targetPrevEntry, targetPrevRid);
			RETURN_ON_ERR(ret);
		}
		else if (!atEnd) // start
		{
			// Insert the new record into the root, and make it point to the current RID
			entry.nextSlot = footer->firstRecord;
			RID newEntry;
			cout << "Inserting to start of page: " << page << endl;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &entry, page, newEntry);
			RETURN_ON_ERR(ret);

			// We are the beginning of the list, update the firstRecord slot to point here
			ret = fileHandle.readPage(footer->pageNumber, pageBuffer);
			RETURN_ON_ERR(ret);

			footer->firstRecord = newEntry;
			cout << "Updating first record of " << footer->pageNumber << " to " << newEntry.pageNum << "," << newEntry.slotNum << endl;
			ret = fileHandle.writePage(footer->pageNumber, pageBuffer);
			RETURN_ON_ERR(ret);
		}
		else // append the RID to the end of the on-page list 
		{
			// Insert the new record into the root
			RID newEntry;
			cout << "Inserting to end of page: " << page << endl;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &entry, page, newEntry);
			RETURN_ON_ERR(ret);

			// Update the previous entry to point to the new entry
			targetEntry.nextSlot = newEntry;
			cout << "Updating end: " << page << "," << targetRid.pageNum << "," << targetRid.slotNum << endl;
			ret = updateRecord(fileHandle, recordDescriptor, &targetEntry, targetRid);
			RETURN_ON_ERR(ret);
		}
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

	// Set up the leaf data struct
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
	RETURN_ON_ERR(ret);

	// Determine if we can fit on this page
	unsigned targetFreeSpace = calculateFreespace(footer->freeSpaceOffset, footer->numSlots);
	cout << "New entry size = " << recLength << endl;
	cout << "Target freespace = " << targetFreeSpace << endl;
	if ((recLength + sizeof(PageIndexSlot)) > targetFreeSpace)
	{
		if (footer->numSlots == 0)
			return rc::BTREE_KEY_TOO_LARGE;

		return rc::BTREE_INDEX_PAGE_FULL;
	}

	// Special case if the leaf is empty
	if (footer->numSlots == 0)
	{
		// Insert the new record into the root
		RID newEntry;
		ret = insertRecordToPage(fileHandle, recordDescriptor, &leaf, page, newEntry);
		RETURN_ON_ERR(ret);

		// Pull in the updated footer
		ret = fileHandle.readPage(page, pageBuffer);
		RETURN_ON_ERR(ret);
		
		// Update the header of the page to point to this new entry
		footer->firstRecord = newEntry;
		cout << "Updating first record of " << page << " to " << newEntry.pageNum << "," << newEntry.slotNum << endl;
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
		bool found = false;
		bool atEnd = false;
		RID currRid = footer->firstRecord;
		RID prevRid;
		RID targetPrevRid;
		RID targetRid = footer->firstRecord;
		IndexRecord targetPrevEntry;
		IndexRecord targetEntry;
		IndexRecord currEntry;
		IndexRecord prevEntry;

		int numEntries = 0;
		int target = 0;
		while (currRid.pageNum > 0) 
		{
			// Pull in the next entry
			// cout << "Reading: " << currRid.pageNum << "," << currRid.slotNum << endl;
			ret = readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
			RETURN_ON_ERR(ret);

			int compareResult = 0;
			ret = keyData.compare(attribute.type, currEntry.key, compareResult);
			RETURN_ON_ERR(ret);

            if (compareResult >= 0)
			{
				target = numEntries;
				targetPrevRid = prevRid;
				targetRid = currRid;
				targetEntry = currEntry;
				targetPrevEntry = prevEntry;
			}
			numEntries++;

            // Update for next iteration
            prevRid = currRid;
            prevEntry = currEntry;
            currRid = currEntry.nextSlot;
		}

		// Drop the record into the right spot in the on-page list
		atEnd = (target + 1) == numEntries;
		if (!atEnd && targetPrevRid.pageNum > 0 && numEntries > 1) // middle of list
		{
			// Insert the new record into the root, and make it point to the current RID
			leaf.nextSlot = targetRid;
			RID newEntry;
			cout << "Inserting to page: " << page << endl;
			cout << calculateFreespace(footer->freeSpaceOffset, footer->numSlots) << endl;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &leaf, page, newEntry);
			RETURN_ON_ERR(ret);

			// Update the previous entry to point to the new entry (it's in the middle now)
			targetPrevEntry.nextSlot = newEntry;
			ret = updateRecord(fileHandle, recordDescriptor, &targetPrevEntry, targetPrevRid);
			RETURN_ON_ERR(ret);
		}
		else if (!atEnd)
		{
			// Insert the new record into the root, and make it point to the current RID
			leaf.nextSlot = footer->firstRecord;
			RID newEntry;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &leaf, page, newEntry);
			RETURN_ON_ERR(ret);

			// We are the beginning of the list, update the firstRecord slot to point here
			ret = fileHandle.readPage(footer->pageNumber, pageBuffer);
			RETURN_ON_ERR(ret);

			footer->firstRecord = newEntry;
			ret = fileHandle.writePage(footer->pageNumber, pageBuffer);
			RETURN_ON_ERR(ret);
		}
		else // append the RID to the end of the on-page list 
		{
			// Insert the new record into the root
			RID newEntry;
			ret = insertRecordToPage(fileHandle, recordDescriptor, &leaf, page, newEntry);
			RETURN_ON_ERR(ret);

			// Update the previous entry to point to the new entry
			targetEntry.nextSlot = newEntry;
			cout << "Updating end: " << page << "," << targetRid.pageNum << "," << targetRid.slotNum << endl;
			ret = updateRecord(fileHandle, recordDescriptor, &targetEntry, targetRid);
			RETURN_ON_ERR(ret);
		}
	}

	return rc::OK;
}

RC IndexManager::findNonLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, PageNum& pageNum)
{
	RID targetRid, previousRid;
	RC ret = findNonLeafIndexEntry(fileHandle, footer, attribute, key, targetRid, previousRid);
	RETURN_ON_ERR(ret);

	if (targetRid.pageNum == 0)
	{
		pageNum = footer->leftChild;
	}
	else
	{
		pageNum = targetRid.pageNum;
	}

	return rc::OK;
}

RC IndexManager::findNonLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, RID& targetRid, RID& prevRid)
{
	int compareResult = 1;
	prevRid.slotNum = prevRid.pageNum = 0;
	targetRid.slotNum = prevRid.slotNum = 0;

	// Extract the first record
	RID currRid = footer->firstRecord;
	IndexRecord currEntry;

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	// Is our comparision value less than all of the keys on this page?
	assert(footer->numSlots >= 1);
	RC ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
	RETURN_ON_ERR(ret);

	if (key)
	{
		ret = key->compare(attribute.type, currEntry.key, compareResult);
		RETURN_ON_ERR(ret);
	}

	if (compareResult < 0)
	{
		cout << "BRANCHING TO LEFTMOST POINTER" << endl;
		targetRid.pageNum = targetRid.slotNum = 0;
	}
	else
	{
		// Traverse the list of index entries on this non-leaf page
		while (currRid.pageNum > 0)
		{
			// Pull in the new entry and perform the comparison
			RC ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
			RETURN_ON_ERR(ret);

			if (key)
			{
				ret = key->compare(attribute.type, currEntry.key, compareResult);
				RETURN_ON_ERR(ret);
			}

			if (compareResult >= 0)
			{
				targetRid = currEntry.rid;
			}
			
			currRid = currEntry.nextSlot;
		}
	}

	prevRid = currRid;
	return rc::OK;
}

RC IndexManager::readRootPage(FileHandle& fileHandle, void* pageBuffer)
{
	// Read in the reserved page
	RC ret = fileHandle.readPage(0, pageBuffer);
	RETURN_ON_ERR(ret);

	// Place the root page data at the very end of the page
	unsigned* rootPage = (unsigned*)( (char*)pageBuffer + PAGE_SIZE - sizeof(unsigned) );

	// Pull in the root page
	ret = fileHandle.readPage(*rootPage, pageBuffer);
	RETURN_ON_ERR(ret);

	return rc::OK;
}

RC IndexManager::findLargestLeafIndexEntry(FileHandle& fileHandle, const Attribute& attribute, RID& rid)
{
	// Pull in the root page
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = readRootPage(fileHandle, pageBuffer);
	RETURN_ON_ERR(ret);

	// Extract the header
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);
	const PageNum rootPageNum = footer->pageNumber;

	// Extract the first record
	RID currRid = footer->firstRecord;
	IndexRecord currEntry;

	// Determine what type of record descriptor we need
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);

	// Traverse down the tree to the rightmost leaf, using non-leaves along the way
	PageNum currentPage = rootPageNum;
	while (footer->isLeafPage == false)
	{
		ret = findNonLeafIndexEntry(fileHandle, footer, attribute, NULL, currentPage);
		RETURN_ON_ERR(ret);

		// Pull the designated page into memory and refresh the footer
		memset(pageBuffer, 0, PAGE_SIZE);
		ret = fileHandle.readPage(currentPage, pageBuffer);
		RETURN_ON_ERR(ret);
	}

	// We're at a leaf now...
	assert(footer->isLeafPage);

	// Find the largest record on this leaf
	currRid = footer->firstRecord;
	RID largestRid = currRid;

	while(currRid.pageNum != 0)
	{
		largestRid = currRid;

		// Pull in the record
		RC ret = IndexManager::instance()->readRecord(fileHandle, recordDescriptor, currRid, &currEntry);
		RETURN_ON_ERR(ret);

		// Advance to the next record
		currRid = currEntry.nextSlot;
	}

	// Return the largest record we found
	rid = largestRid;

	return rc::OK;
}

RC IndexManager::findSmallestLeafIndexEntry(FileHandle& fileHandle, RID& rid)
{
	// Begin at the root
	char pageBuffer[PAGE_SIZE] = {0};
	RC ret = readRootPage(fileHandle, pageBuffer);
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

RC IndexManager::findLeafIndexEntry(FileHandle& fileHandle, const Attribute &attribute, KeyValueData* key, RID& entryRid, RID& prevEntryRid, RID& nextEntryRid, RID& dataRid)
{
	// Begin at the root
	char pageBuffer[PAGE_SIZE] = {0};
	RC ret = readRootPage(fileHandle, pageBuffer);
	RETURN_ON_ERR(ret);

	IX_PageIndexFooter* footer = IndexManager::getIXPageIndexFooter(pageBuffer);
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

RC IndexManager::deletelessSplit(FileHandle& fileHandle, const std::vector<Attribute>& recordDescriptor, PageNum& targetPageNum, PageNum& newPageNum, RID& rightRid, KeyValueData& rightKey)
{
	// Read in the page to be split
	unsigned char inputBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(targetPageNum, inputBuffer);
	IX_PageIndexFooter* inputFooter = getIXPageIndexFooter(inputBuffer);
	RETURN_ON_ERR(ret);

	// Allocate the new page and save its reference
	newPageNum = fileHandle.getNumberOfPages();
	rightRid.pageNum = newPageNum;
	ret = newPage(fileHandle, newPageNum, inputFooter->isLeafPage, inputFooter->nextLeafPage, 0);
	RETURN_ON_ERR(ret);

	// Clear out the old page data
	ret = newPage(fileHandle, targetPageNum, inputFooter->isLeafPage, newPageNum, inputFooter->leftChild);
	RETURN_ON_ERR(ret);

	// Setup buffers for left/right page outputs
	unsigned char leftBuffer[PAGE_SIZE] = {0};
	unsigned char rightBuffer[PAGE_SIZE] = {0};
	IX_PageIndexFooter* leftFooter = getIXPageIndexFooter(leftBuffer);
	IX_PageIndexFooter* rightFooter = getIXPageIndexFooter(rightBuffer);

	ret = fileHandle.readPage(targetPageNum, leftBuffer);
	RETURN_ON_ERR(ret);

	ret = fileHandle.readPage(newPageNum, rightBuffer);
	RETURN_ON_ERR(ret);

	std::cout << " SPLITTING " << targetPageNum << " + " << newPageNum << std::endl;
	std::cout << "Leaf? " << inputFooter->isLeafPage << endl;

	// Update the nextLeaf pointer if needed
	if (inputFooter->isLeafPage)
	{
		inputFooter->nextLeafPage = newPageNum;
		leftFooter->nextLeafPage = newPageNum;
	}
	
	assert(rightFooter->isLeafPage == inputFooter->isLeafPage && leftFooter->isLeafPage == inputFooter->isLeafPage);

	// Update known footer data
	rightFooter->parent = inputFooter->parent;
	leftFooter->parent = inputFooter->parent;

	// Read in records and write to the left page until we hit half of the page full
	IndexRecord tempRecord;
	unsigned currentSize = sizeof(IX_PageIndexFooter);
	unsigned slotNum = 0;
	RID lastLeftRid, prevRid, curRid = inputFooter->firstRecord;
	while(currentSize < PAGE_SIZE/2 && curRid.pageNum > 0)
	{
		// Read in the record from our input buffer
		prevRid = curRid;
		ret = readRecord(fileHandle, recordDescriptor, curRid, &tempRecord, inputBuffer);
		RETURN_ON_ERR(ret);

		// Compute the record length and add it to the running total
		unsigned recLength = 0;
		unsigned recHeaderSize = 0;
		unsigned* recHeader = NULL;
		ret = generateRecordHeader(recordDescriptor, &tempRecord, recHeader, recLength, recHeaderSize);
		currentSize += recLength + sizeof(PageIndexSlot);
		RETURN_ON_ERR(ret);

		// Advance to next record
		curRid = tempRecord.nextSlot;
		++slotNum;

		// We know what the slot numbers will be because the page is empty initially
		if (currentSize >= PAGE_SIZE/2 || curRid.pageNum == 0)
		{
			tempRecord.nextSlot.pageNum = 0;
			tempRecord.nextSlot.slotNum = 0;
		}
		else
		{
			tempRecord.nextSlot.pageNum = leftFooter->pageNumber;
			tempRecord.nextSlot.slotNum = slotNum;
		}

		// Copy over the record to the new left page buffer
		ret = insertRecordInplace(recordDescriptor, &tempRecord, leftFooter->pageNumber, leftBuffer, lastLeftRid);
		RETURN_ON_ERR(ret);
	}

	std::cout << "Records still on LEFT page = " << slotNum << " @ " << currentSize << " bytes" << std::endl;

	// Read in the first entry in the second half - this entry now points to the left
	if (!inputFooter->isLeafPage)
	{
		// Load in the entry
		ret = readRecord(fileHandle, recordDescriptor, curRid, &tempRecord, inputBuffer);
		RETURN_ON_ERR(ret);

		std::cout << " LEAF split, so RIGHT will have a leftChild = ";
		//tempRecord.key.print(TypeInt);
		std::cout << std::endl;
		
		// Copy over the page pointer into the footer
		rightFooter->leftChild = curRid.pageNum;
		curRid = tempRecord.nextSlot;
	}

	// Save the 1st key that will be on the right page for later
	const RID firstRightPageRid = curRid;
	cout << "Right key: " << firstRightPageRid.pageNum << "," << firstRightPageRid.slotNum << endl;

	ret = readRecord(fileHandle, recordDescriptor, firstRightPageRid, &tempRecord, inputBuffer);
	memcpy(&rightKey, &tempRecord.key, sizeof(rightKey));
	RETURN_ON_ERR(ret);

	std::cout << " Break connection on RIGHT: ";
	//rightKey.print(TypeInt);
	std::cout << std::endl;

	// The currRid variable now points to the correct spot in the list from which we should start moving
	// Move the rest over to the new page
	currentSize = sizeof(IX_PageIndexFooter);
	slotNum = 0;
	RID lastRightRid;
	while (curRid.pageNum > 0)
	{
		prevRid = curRid;

		// Read in the record from our input buffer
		ret = readRecord(fileHandle, recordDescriptor, curRid, &tempRecord, inputBuffer);
		RETURN_ON_ERR(ret);

		// Compute the record length and add it to the running total
		unsigned recLength = 0;
		unsigned recHeaderSize = 0;
		unsigned* recHeader = NULL;
		ret = generateRecordHeader(recordDescriptor, &tempRecord, recHeader, recLength, recHeaderSize);
		currentSize += recLength + sizeof(PageIndexSlot);
		RETURN_ON_ERR(ret);

		// Advance to next record
		curRid = tempRecord.nextSlot;
		++slotNum;

		// We know what the slot numbers will be because the page is empty initially
		if (curRid.pageNum == 0)
		{
			tempRecord.nextSlot.pageNum = 0;
			tempRecord.nextSlot.slotNum = 0;
		}
		else
		{
			tempRecord.nextSlot.pageNum = rightFooter->pageNumber;
			tempRecord.nextSlot.slotNum = slotNum;
		}

		// Copy over the record to the new right page buffer
		ret = insertRecordInplace(recordDescriptor, &tempRecord, rightFooter->pageNumber, rightBuffer, lastRightRid);
		RETURN_ON_ERR(ret);
	}

	cout << "TOTAL INSERTED ON RIGHT page " << newPageNum << ": " << slotNum << endl;

	// Write out the new page buffers
	ret = fileHandle.writePage(targetPageNum, leftBuffer);
	RETURN_ON_ERR(ret);
	ret = fileHandle.writePage(newPageNum, rightBuffer);
	RETURN_ON_ERR(ret);

	return rc::OK;
}

RC IndexManager::getNextRecord(FileHandle& fileHandle, const std::vector<Attribute>& recordDescriptor, const Attribute& attribute, RID& rid)
{
	if (rid.pageNum == 0)
		return IX_EOF;

	// Read in the page with current record
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
	RETURN_ON_ERR(ret);

	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);
	if (!footer->isLeafPage)
		return rc::BTREE_ITERATOR_ILLEGAL_NON_LEAF_RECORD;
	if (footer->numSlots <= rid.slotNum)
		return rc::BTREE_INDEX_LEAF_ENTRY_NOT_FOUND;

	IndexRecord record;
	ret = readRecord(fileHandle, recordDescriptor, rid, &record, pageBuffer);
	RETURN_ON_ERR(ret);

	// We have a valid next slot, use it and return
	if (record.nextSlot.pageNum != 0)
	{
		rid = record.nextSlot;
		return rc::OK;
	}

	// We need to load in a new page
	ret = fileHandle.readPage(footer->nextLeafPage, pageBuffer);
	RETURN_ON_ERR(ret);

	if (footer->firstRecord.pageNum == 0)
		return rc::BTREE_INDEX_LEAF_ENTRY_NOT_FOUND;

	// Our first record on the next leaf page is the next record
	rid = footer->firstRecord;
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
	_im(*IndexManager::instance())
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

	_recordDescriptor = _im.getIndexRecordDescriptor(attribute.type);
	RETURN_ON_ERR(ret);

	//IndexManager::instance()->printIndex(*fileHandle, attribute, true);

	// Traverse down the left pointers to find the lowest RID
	RID lowestPossibleRid;
	ret = IndexManager::findSmallestLeafIndexEntry(*_fileHandle, lowestPossibleRid);
	RETURN_ON_ERR(ret);

	// Copy over the key values to local memory
	if (lowKey)
	{
		if (_lowKeyValue)
				free(_lowKeyValue);

		_lowKeyValue = new KeyValueData();
		if (!_lowKeyValue)
		{
			return rc::OUT_OF_MEMORY;
		}

		// Copy over the key that we will be comparing against on the low end
		ret = _lowKeyValue->init(attribute.type, lowKey);
		if (ret != rc::OK)
		{
			free(_lowKeyValue);
			_lowKeyValue = NULL;
			RETURN_ON_ERR(ret);
			assert(false);
		}

		// Look for something that matches our low end
		RID entryRid, prevEntryRid, nextEntryRid, dataRid;
		ret = IndexManager::findLeafIndexEntry(*fileHandle, attribute, _lowKeyValue, entryRid, prevEntryRid, nextEntryRid, dataRid);
		RETURN_ON_ERR(ret);

		if (entryRid.pageNum == 0)
		{
			// TODO: When is this true?
			_beginRecordRid = lowestPossibleRid;
		}
		else if (_lowKeyInclusive)
		{
			_beginRecordRid = entryRid;
		}
		else
		{
			_beginRecordRid = nextEntryRid;
		}
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
		if (_highKeyValue)
				free(_highKeyValue);

		_highKeyValue = new KeyValueData();
		if (!_highKeyValue)
		{
			return rc::OUT_OF_MEMORY;
		}

		ret = _highKeyValue->init(attribute.type, highKey);
		if (ret != rc::OK)
		{
			free(_highKeyValue);
			_highKeyValue = NULL;
			RETURN_ON_ERR(ret);
			assert(false);
		}

		// Look for something that matches our high end
		RID entryRid, prevEntryRid, nextEntryRid, dataRid;
		ret = IndexManager::findLeafIndexEntry(*fileHandle, attribute, _highKeyValue, entryRid, prevEntryRid, nextEntryRid, dataRid);
		RETURN_ON_ERR(ret);

		if (entryRid.pageNum == 0)
		{
			// TODO: When is this true?
			ret = IndexManager::findLargestLeafIndexEntry(*fileHandle, attribute, _endRecordRid);
			RETURN_ON_ERR(ret);
		}
		else
		{
			_endRecordRid = entryRid;
		}
	}
	else
	{
		if (_highKeyValue)
			free(_highKeyValue);

		_highKeyValue = NULL;
		ret = IndexManager::findLargestLeafIndexEntry(*fileHandle, attribute, _endRecordRid);
		RETURN_ON_ERR(ret);
	}

	// Start at the beginning RID (keep track of beginRecordRid just for debug purposes for now)
	_currentRecordRid = _beginRecordRid;

	// Get the next RID also
	if (_currentRecordRid.pageNum != 0)
	{
		_nextRecordRid = _currentRecordRid;
		ret = _im.getNextRecord(*_fileHandle, _recordDescriptor, _attribute, _nextRecordRid);
		RETURN_ON_ERR(ret);
	}

	return rc::OK;
}

RC IX_ScanIterator::getNextEntry(RID &rid, void *key)
{
	RC ret = rc::OK;
	if (_currentRecordRid.pageNum == 0)
		return IX_EOF;
	
	// Check to see if we're at the beginning
	if (_currentRecordRid.pageNum == _beginRecordRid.pageNum && _currentRecordRid.slotNum == _beginRecordRid.slotNum)
	{
		// If we are not inclusive, skip the 1st record
		if (!_lowKeyInclusive)
		{
			ret = advance();
			RETURN_ON_ERR(ret);
		}
	}

	// Check to see if we're at the end
	if (_currentRecordRid.pageNum == _endRecordRid.pageNum && _currentRecordRid.slotNum == _endRecordRid.slotNum)
	{
		// If we are not inclusive, skip the record
		if (!_highKeyInclusive)
		{
			_currentRecordRid.pageNum = 0;
		}
	}

	// TODO: Make sure we support very small ranges, of size 1 or 2 with all types of inclusivity
	if (_currentRecordRid.pageNum == 0)
	{
		return IX_EOF;
	}

	// We have a valid record, pull the data RID and key value and return it
	RID targetRid = _currentRecordRid;	
	IndexRecord record;
	ret = _im.readRecord(*_fileHandle, _recordDescriptor, _currentRecordRid, &record);
	rid = record.rid;
	switch (_attribute.type)
	{
		case TypeInt:
			memcpy(key, &record.key.integer, sizeof(unsigned));
			break;
		case TypeReal:
			memcpy(key, &record.key.real, sizeof(float));
			break;
		case TypeVarChar:
			int size = 0;
			memcpy(&size, record.key.varchar, sizeof(unsigned));
			memcpy(key, record.key.varchar + sizeof(unsigned), size);
	}

	// Advance to the next RID
	if (ret != rc::OK && ret == rc::RECORD_DELETED)
	{
		_currentRecordRid = _nextRecordRid;
		rid = _nextRecordRid;
		ret = _im.readRecord(*_fileHandle, _recordDescriptor, _nextRecordRid, key);
		RETURN_ON_ERR(ret);
	}

	// Advance to the next record
	ret = advance();
	RETURN_ON_ERR(ret);

    return rc::OK;
}

RC IX_ScanIterator::advance()
{
	RC ret = _im.getNextRecord(*_fileHandle, _recordDescriptor, _attribute, _currentRecordRid);
	
	// If we are off the edge, our scan is done
	if (ret != rc::OK && ret == rc::BTREE_INDEX_LEAF_ENTRY_NOT_FOUND)
	{
		_currentRecordRid.pageNum = _currentRecordRid.slotNum = 0;
		_nextRecordRid.pageNum = _nextRecordRid.slotNum = 0;
		return rc::OK;
	}

	RETURN_ON_ERR(ret);

	if (_currentRecordRid.pageNum == 0)
	{
		_nextRecordRid.pageNum = 0;
	}
	else
	{
		_nextRecordRid = _currentRecordRid;
		ret = _im.getNextRecord(*_fileHandle, _recordDescriptor, _attribute, _nextRecordRid);

		// nextRecord will go off the edge before we are done scanning, so allow it to fail
		if (ret != rc::OK && ret != rc::BTREE_INDEX_LEAF_ENTRY_NOT_FOUND)
		{
			RETURN_ON_ERR(ret);
		}
	}

	return rc::OK;
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
			size = *((unsigned*)(key));
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
			result = strncmp(varchar + sizeof(unsigned), that.varchar + sizeof(unsigned), size);
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

void KeyValueData::print(AttrType type)
{
	switch (type)
	{
		case TypeInt:
			std::cout << integer;
			break;

		case TypeReal:
			std::cout << real;
			break;

		case TypeVarChar:
			{
				char stringBuffer[MAX_KEY_SIZE+1] = {0};
				memcpy(stringBuffer, varchar+4, size);
				std::string s(stringBuffer);

				if (size==1)
				{
					std::cout << "'" << s << "'(";

					if (s[0] < 0)
						std::cout << ((int)s[0]+256);
					else
						std::cout << ((int)s[0]);

					std::cout << ")";
				}
				else
				{
					std::cout << "(" << size << ")'" << s << "'";
				}
				
			}
			break;

		default:
			std::cout << "????";
	}
}

std::ostream& operator<<(std::ostream& os, const IX_PageIndexFooter& f)
{
	if (f.isLeafPage)
	{
		os << "Leaf Page: ";
		os << "firstRecord: " << f.firstRecord;
		os << " Parent=" << f.parent << " leftChild=" << f.leftChild << " nextLeafPage=" << f.nextLeafPage;
		os << " gap=" << f.gapSize << " free=" << IndexManager::instance()->calculateFreespace(f.freeSpaceOffset, f.numSlots, sizeof(IX_PageIndexFooter)) << " slots=" << f.numSlots;
	}
	else
	{
		os << "Non-Leaf Page: ";
		os << "firstRecord=" << f.firstRecord;
		os << " Parent=" << f.parent << " leftChild=" << f.leftChild;
		os << " gap=" << f.gapSize << " free=" << IndexManager::instance()->calculateFreespace(f.freeSpaceOffset, f.numSlots, sizeof(IX_PageIndexFooter)) << " slots=" << f.numSlots;
	}

	return os;
}

void IndexRecord::print(AttrType type, bool isLeaf)
{
	key.print(type);
	std::cout << "\tnext=" << nextSlot << " ";
	if (isLeaf)
		std::cout << "\tdata=" << rid;
	else
		std::cout << "\tpage=" << rid;
}

RC IndexManager::printIndex(FileHandle& fileHandle, const Attribute& attribute, bool extended)
{
	IndexManager* im = IndexManager::instance();

	std::cout << "BEGIN==============" << fileHandle.getFilename() << "==============" << std::endl;
	
	const std::vector<Attribute>& recordDescriptor = getIndexRecordDescriptor(attribute.type);
	std::cout << "RecordDescriptor: [ ";
	for (std::vector<Attribute>::const_iterator it=recordDescriptor.begin(); it!=recordDescriptor.end(); ++it)
	{
		std::cout << (*it) << " ";
	}
	std::cout << "]" << std::endl;
	
	std::cout << "Pages: " << fileHandle.getNumberOfPages();

	PageNum currentPage = 1;
	IndexRecord currEntry;
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	IX_PageIndexFooter* footer = getIXPageIndexFooter(pageBuffer);

	std::cout << "\tRoot: " << footer->pageNumber << "\n" << std::endl;

	// Loop through all pages and print a summary of the data
	while(currentPage < fileHandle.getNumberOfPages())
	{
		RC ret = fileHandle.readPage(currentPage, pageBuffer);
		RETURN_ON_ERR(ret);

		// Print out page basics
		std::cout << "---Page: " << currentPage << "  " << *footer;
		std::cout << std::endl;

		std::cout << "Record List: ";
		{
			// Print out all records pointed to on this page
			RID curRid = footer->firstRecord;
			while (curRid.pageNum > 0)
			{
				ret = im->readRecord(fileHandle, recordDescriptor, curRid, &currEntry, pageBuffer);
				RETURN_ON_ERR(ret);

				std::cout << curRid.slotNum << " > ";
				curRid = currEntry.nextSlot;
			}
		}

		std::cout << "x" << std::endl;
		if (extended)
		{
			// Print out all the slots on this page
			RID curRid;
			curRid.pageNum = footer->pageNumber;
			curRid.slotNum = 0;

			while(curRid.slotNum < footer->numSlots)
			{
				ret = im->readRecord(fileHandle, recordDescriptor, curRid, &currEntry, pageBuffer);
				if (ret != rc::OK)
				{
					if (ret == rc::RECORD_DELETED)
					{
						std::cout << "s=" << curRid.slotNum << "\tDELETED\n";
						curRid.slotNum++;
						continue;
					}
					else
					{
						RETURN_ON_ERR(ret);
					}
				}

				std::cout << "s=" << curRid.slotNum << "\tkey=";
				currEntry.print(attribute.type, footer->isLeafPage);
				std::cout << std::endl;

				curRid.slotNum++;
			}
		}
		else
		{
			// Print out all records pointed to on this page
			RID curRid = footer->firstRecord;
			while (curRid.pageNum > 0)
			{
				ret = im->readRecord(fileHandle, recordDescriptor, curRid, &currEntry, pageBuffer);
				RETURN_ON_ERR(ret);

				std::cout << "s=" << curRid.slotNum << "\tkey=";
				currEntry.print(attribute.type, footer->isLeafPage);
				std::cout << std::endl;

				curRid = currEntry.nextSlot;
			}
		}

		std::cout << std::endl;
		++currentPage;
	}

	std::cout << "==============" << fileHandle.getFilename() << "==============END" << std::endl;
	return rc::OK;
}
