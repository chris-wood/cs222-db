#include "../util/returncodes.h"
#include "rbcm.h"

#include <assert.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <new>

#ifdef REDIRECT_PRINT_RECORD
#include <fstream>
#endif

RecordBasedCoreManager::RecordBasedCoreManager(unsigned slotOffset)
    : _pfm(*PagedFileManager::instance()), _pageSlotOffset(slotOffset)
{
}

RecordBasedCoreManager::~RecordBasedCoreManager()
{
}

RC RecordBasedCoreManager::createFile(const string &fileName) {
    RC ret = _pfm.createFile(fileName.c_str());
    if (ret != rc::OK)
    {
        return ret;
    }

    if (sizeof(PFHeader) > PAGE_SIZE)
    {
        return rc::HEADER_SIZE_TOO_LARGE;
    }

    FileHandle handle;
    PFHeader header;
    header.init();

    // Write out the header data to the reserved page (page 0)
    ret = _pfm.openFile(fileName.c_str(), handle);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Initialize and validate the header for the new file
    ret = header.validate();
    if (ret != rc::OK)
    {
        return ret;
    }

    // Flush the header page to disk
    ret = writeHeader(handle, &header);
    if (ret != rc::OK)
    {
        _pfm.closeFile(handle);
        return ret;
    }

    // Drop the handle to the file
    ret = _pfm.closeFile(handle);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::OK;
}

RC RecordBasedCoreManager::destroyFile(const string &fileName) {
    return _pfm.destroyFile(fileName.c_str());
}

RC RecordBasedCoreManager::openFile(const string &fileName, FileHandle &fileHandle) {
    RC ret = _pfm.openFile(fileName.c_str(), fileHandle);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Read in header data from the reserved page
    PFHeader header;
    ret = readHeader(fileHandle, &header);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Validate header is in our correct format
    ret = header.validate();
    return ret;
}

RC RecordBasedCoreManager::closeFile(FileHandle &fileHandle) {
    RC ret = _pfm.closeFile(fileHandle);
    if (ret != rc::OK)
    {
        return ret;
    }
    return rc::OK;
}

// Find a page (or insert a new one) that has at least 'bytes' free on it 
RC RecordBasedCoreManager::findFreeSpace(FileHandle &fileHandle, unsigned bytes, PageNum& pageNum)
{
    RC ret;
    PFHeader header;
    readHeader(fileHandle, &header);

    if (bytes < 1)
    {
        return rc::RECORD_SIZE_INVALID;
    }

    // Search through the freespace lists, starting with the smallest size and incrementing upwards
    for (unsigned i=0; i<header.numFreespaceLists; ++i)
    {
        const FreeSpaceList& freespaceList = header.freespaceLists[i];
        if (freespaceList.cutoff >= bytes)
        {
            if (freespaceList.listHead != 0)
            {
                pageNum = freespaceList.listHead;
                return rc::OK;
            }
        }
    }

    // If we did not find a suitible location, append however many pages we need to store the data
    unsigned char listSwapBuffer[PAGE_SIZE] = {0};
    unsigned requiredPages = 1 + ((bytes - 1) / PAGE_SIZE);
    unsigned char* newPages = (unsigned char*)malloc(requiredPages * PAGE_SIZE);
	if (!newPages)
	{
		return rc::OUT_OF_MEMORY;
	}
	
	// "Zero" out memory for cleanliness
    memset(newPages, 255, PAGE_SIZE * requiredPages);
	memset(listSwapBuffer, 255, PAGE_SIZE);

    for (unsigned i=0; i<requiredPages; ++i)
    {
        // Create the blank index structure on each page (at the end)
        // Note: there are no slots yet, so there are no slotOffsets prepended to this struct on disk
        // PageIndexHeader* index = getPageIndexHeader(newPages);
        CorePageIndexHeader *index = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));

        index->freeSpaceOffset = 0;
        index->numSlots = 0;
        index->gapSize = 0;
        index->pageNumber = fileHandle.getNumberOfPages();
        index->prevPage = 0;
        index->nextPage = 0;
        index->freespaceList = header.numFreespaceLists - 1; // This is an empty page right now, put it in the largest slot

        // Append this page to the list of free pages (into the beginning of the largest freespace slot)
        FreeSpaceList& oldFreeSpaceList = header.freespaceLists[index->freespaceList];
        if (oldFreeSpaceList.listHead == 0)
        {
            oldFreeSpaceList.listHead = index->pageNumber;
        }
        else
        {
            // Read in the previous list head
            ret = fileHandle.readPage(oldFreeSpaceList.listHead, listSwapBuffer);
            if (ret != rc::OK)
            {
                free(newPages);
                return ret;
            }

            // Update the prev pointer of the previous head to be our page number
            // PageIndexHeader* previousListHead = getPageIndexHeader(listSwapBuffer);
            char* prevHeaderBuffer = (char*)getPageIndexHeader(listSwapBuffer, _pageSlotOffset);
            CorePageIndexHeader *previousListHead = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
            memcpy(previousListHead, prevHeaderBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

            previousListHead->prevPage = index->pageNumber;
            memcpy(listSwapBuffer + PAGE_SIZE - sizeof(CorePageIndexHeader), previousListHead, sizeof(CorePageIndexHeader));

            ret = fileHandle.writePage(previousListHead->pageNumber, listSwapBuffer);
            if (ret != rc::OK)
            {
                free(newPages);
                return ret;
            }

            // Update the header info so the list points to the newly created page as the head
            oldFreeSpaceList.listHead = index->pageNumber;
            index->nextPage = previousListHead->pageNumber;
        }

        // Append this new page to the file
        memcpy(newPages + PAGE_SIZE - _pageSlotOffset - (_pageSlotOffset - sizeof(CorePageIndexHeader)), index, sizeof(CorePageIndexHeader));
        ret = fileHandle.appendPage(newPages);
        free(newPages);

        if (ret != rc::OK)
        {
            return ret;
        }
    }

    // Store the page number base where this record is stored
    pageNum = fileHandle.getNumberOfPages() - requiredPages; 

    // Update the file header information immediately
    header.numPages = header.numPages + requiredPages;
    ret = writeHeader(fileHandle, &header);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::OK;
}

RC RecordBasedCoreManager::movePageToCorrectFreeSpaceList(FileHandle &fileHandle, void* pageHeaderBuffer)
{
    PFHeader header;
    readHeader(fileHandle, &header);

    CorePageIndexHeader *pageHeader = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
    memcpy(pageHeader, (char*)pageHeaderBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

    int freespace = calculateFreespace(pageHeader->freeSpaceOffset, pageHeader->numSlots, _pageSlotOffset);
	assert(freespace >= 0);

    for (unsigned i=header.numFreespaceLists; i>0; --i)
    {
        unsigned listIndex = i - 1;
        if (freespace >= header.freespaceLists[listIndex].cutoff)
        {
            return movePageToFreeSpaceList(fileHandle, pageHeaderBuffer, listIndex);
        }
    }

	// Backup plan, if we somehow ruined our page somewhow
	return movePageToFreeSpaceList(fileHandle, pageHeader, 0);
}

RC RecordBasedCoreManager::movePageToFreeSpaceList(FileHandle& fileHandle, void* pageHeaderBuffer, unsigned destinationListIndex)
{
    CorePageIndexHeader *pageHeader = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
    memcpy(pageHeader, (char*)pageHeaderBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

    if (destinationListIndex == pageHeader->freespaceList)
    {
        // Nothing to do, return
        return rc::OK;
    }

    RC ret;
    PFHeader header;
    unsigned char pageBuffer[PAGE_SIZE] = {0};

	ret = readHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

    // Update prevPage to point to our nextPage
    if (pageHeader->prevPage > 0)
    {
        // Read in the next page
        ret = fileHandle.readPage(pageHeader->prevPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the prev pointer of the next page to our prev pointer
        // PageIndexHeader* prevPageHeader = getPageIndexHeader(pageBuffer);
        char* prevHeaderBuffer = (char*)getPageIndexHeader(pageBuffer, _pageSlotOffset);
        CorePageIndexHeader *prevPageHeader = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
        memcpy(prevPageHeader, prevHeaderBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

        prevPageHeader->nextPage = pageHeader->nextPage;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(CorePageIndexHeader), prevPageHeader, sizeof(CorePageIndexHeader));

        ret = fileHandle.writePage(prevPageHeader->pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }
    else
    {
        // We were the beginning of the list, update the head pointer in the file header
        header.freespaceLists[pageHeader->freespaceList].listHead = pageHeader->nextPage;
    }

    // Update nextPage to point to our prevPage
    if (pageHeader->nextPage > 0)
    {
        // Read in the next page

        ret = fileHandle.readPage(pageHeader->nextPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the prev pointer of the next page to our prev pointer
        // PageIndexHeader* nextPageHeader = getPageIndexHeader(pageBuffer);
        char* nextHeaderBuffer = (char*)getPageIndexHeader(pageBuffer, _pageSlotOffset);
        CorePageIndexHeader *nextPageHeader = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
        memcpy(nextPageHeader, nextHeaderBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

        nextPageHeader->prevPage = pageHeader->prevPage;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(CorePageIndexHeader), nextPageHeader, sizeof(CorePageIndexHeader));

        ret = fileHandle.writePage(nextPageHeader->pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }
    // else { we were at the end of the list, we have nothing to do }

    // Update the first page on the destination list to accomodate us at the head
    FreeSpaceList& destinationList = header.freespaceLists[destinationListIndex];
    if (destinationList.listHead > 0)
    {
        // Read in the page

        ret = fileHandle.readPage(destinationList.listHead, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the prev pointer of the previous head to be our page number
        // PageIndexHeader* listFirstPageHeader = getPageIndexHeader(pageBuffer);
        char* listFirstHeaderBuffer = (char*)getPageIndexHeader(pageBuffer, _pageSlotOffset);
        CorePageIndexHeader *listFirstPageHeader = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
        memcpy(listFirstPageHeader, listFirstHeaderBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

        listFirstPageHeader->prevPage = pageHeader->pageNumber;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(CorePageIndexHeader), listFirstPageHeader, sizeof(CorePageIndexHeader));

        ret = fileHandle.writePage(listFirstPageHeader->pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }

    // The next page for us is whatever was the previous 1st page
    pageHeader->nextPage = destinationList.listHead;

    // Update the header's 1st page to our page number
    destinationList.listHead = pageHeader->pageNumber;
    pageHeader->prevPage = 0;

    // Update our freespace index to the new list
    pageHeader->freespaceList = destinationListIndex;

    // Write the header changes back
    memcpy((char*)pageHeaderBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), pageHeader, sizeof(CorePageIndexHeader));

    // Write out the header to disk with new data
    ret = writeHeader(fileHandle, &header);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::OK;
}

RC RecordBasedCoreManager::writeHeader(FileHandle &fileHandle, PFHeader* header)
{
    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedCoreManager::writeHeader(" << fileHandle.getFilename() << ")\n";

    // Copy the header data into a newly allocated buffer
    // We know the only data in page 0 is PFHeader, so we can stomp over everything already there
    unsigned char buffer[PAGE_SIZE] = {0};
    memcpy(buffer, header, sizeof(PFHeader));

    // Commit the header to disk
    RC ret;
    if (fileHandle.getNumberOfPages() == 0)
    {
        ret = fileHandle.appendPage(buffer);
    }
    else
    { 
        ret = fileHandle.writePage(0, buffer);
    }

    return ret;
}

RC RecordBasedCoreManager::readHeader(FileHandle &fileHandle, PFHeader* header)
{
    RC ret = rc::OK;
    unsigned char buffer[PAGE_SIZE] = {0};
    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedCoreManager::readHeader(" << fileHandle.getFilename() << ")\n";

    // If the header page does not exist, create it
    if (fileHandle.getNumberOfPages() == 0)
    {
        PFHeader* blankHeader = (PFHeader*)buffer;
        blankHeader->init();
        ret = fileHandle.appendPage(buffer);
    }

    // Read in the header
    if(ret == rc::OK)
    {
        ret = fileHandle.readPage(0, buffer);
        if (ret != rc::OK)
        {
            return ret;
        }
        memcpy(header, buffer, sizeof(PFHeader));
    }

    // Verify header
    if (ret == rc::OK)
    {
        ret = header->validate();
    }

    return ret;
}

RC RecordBasedCoreManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) 
{    
    // Pull the page into memory - O(1)
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

    // Find the slot where the record is stored, walking tombstone chain if needed - O(1)
	PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum, _pageSlotOffset);
	if (slotIndex->size == 0 && slotIndex->nextPage == 0) // if not a tombstone and empty, it was deleted
	{
		return rc::RECORD_DELETED;
	}
    else if (slotIndex->nextPage > 0) // check to see if we moved to a different page
    {
        unsigned nextPage = slotIndex->nextPage;
        unsigned nextSlot = slotIndex->nextSlot;
        unsigned char tempBuffer[PAGE_SIZE] = {0};
        ret = fileHandle.readPage(nextPage, tempBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
        memcpy(pageBuffer, tempBuffer, PAGE_SIZE);
        slotIndex = getPageIndexSlot(pageBuffer, nextSlot, _pageSlotOffset);
    }

    // Copy the contents of the record into the data block - O(1)
    int fieldOffset = (recordDescriptor.size() * sizeof(unsigned)) + (2 * sizeof(unsigned));
    memcpy(data, pageBuffer + slotIndex->pageOffset + fieldOffset, slotIndex->size - fieldOffset);

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedCoreManager::readRecord: RID = (" << rid.pageNum << ", " << rid.slotNum << ")\n";;
    dbg::out << "RecordBasedCoreManager::readRecord: Reading from: " << PAGE_SIZE - _pageSlotOffset - ((rid.slotNum + 1) * sizeof(PageIndexSlot)) << "\n";;
    dbg::out << "RecordBasedCoreManager::readRecord: Offset: " << slotIndex->pageOffset << "\n";
    dbg::out << "RecordBasedCoreManager::readRecord: Size: " << slotIndex->size << "\n";

    return rc::OK;
}

RC RecordBasedCoreManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) 
{
    unsigned index = 0;
    int offset = 0;
#ifdef REDIRECT_PRINT_RECORD
    // QTCreator has a limitation where you can't redirect to a file from the commandline and still debug
    std::fstream out("out.txt");
#else
    std::ostream& out = std::cout;
#endif

    out << "(";
    for (vector<Attribute>::const_iterator itr = recordDescriptor.begin(); itr != recordDescriptor.end(); itr++)
    {
        Attribute attr = *itr;
        switch (attr.type)
        {
            case TypeInt:
            {
                int* ival = (int*)((char*)data + offset);
                offset += sizeof(int);
                out << *ival;
                break;
            }
            case TypeReal:
            {
                float* rval = (float*)((char*)data + offset);
                offset += sizeof(float);
                out << *rval;
                break;
            }
            case TypeVarChar:
            {
                out << "\"";

                int* count = (int*)((char*)data + offset);
                offset += sizeof(int);
                for (int i=0; i < *count; i++)
                {
                    out << ((char*)data)[offset++];
                }

                out << "\"";
            }
        }

        index++;
        if (index != recordDescriptor.size())
        {
            out << ",";
        }
    }

    out << ")" << endl;

    return rc::OK;
}

RC RecordBasedCoreManager::deleteRecords(FileHandle &fileHandle)
{
	// Pull the file header in
    unsigned char headerBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(0, headerBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}
	PFHeader* pfHeader = (PFHeader*)headerBuffer;

	// O(N) cost - We are rewriting all pages in this file
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	for (unsigned page = 1; page <= pfHeader->numPages; ++page)
	{
        ret = fileHandle.readPage(page, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

		char* headerBuffer = (char*)getPageIndexHeader(pageBuffer, _pageSlotOffset);
        CorePageIndexHeader *pageHeader = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
        memcpy(pageHeader, headerBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

        pageHeader->gapSize = 0;
		pageHeader->numSlots = 0;
		pageHeader->freeSpaceOffset = 0;
		pageHeader->pageNumber = page;
		pageHeader->prevPage = page - 1;
		pageHeader->nextPage = (page < pfHeader->numPages) ? (page - 1) : 0;
		pageHeader->freespaceList = pfHeader->numFreespaceLists - 1;

        memcpy(pageBuffer + PAGE_SIZE - (_pageSlotOffset + sizeof(CorePageIndexHeader)), pageHeader, sizeof(CorePageIndexHeader));

		RC ret = fileHandle.writePage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	return rc::OK;
}

RC RecordBasedCoreManager::deleteRid(FileHandle& fileHandle, const RID& rid, PageIndexSlot* slotIndex, void* headerBuffer, unsigned char* pageBuffer)
{
    // TODO: fetch the number of slots here
    CorePageIndexHeader *header = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
    memcpy(header, (char*)headerBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

	// Overwrite the memory of the record (not absolutely nessecary, but useful for finding bugs if we accidentally try to use it)
    assert(slotIndex->pageOffset + slotIndex->size < (PAGE_SIZE - _pageSlotOffset - (header->numSlots * sizeof(PageIndexSlot))));
	memset(pageBuffer + slotIndex->pageOffset, 255, slotIndex->size);

	// If this is the last record on the page being deleted, merge the freespace with the main pool
	if (rid.slotNum + 1 == header->numSlots)
	{
		// Update the header to merge the freespace pool with this deleted record (and others that are outstanding)
        assert(header->freeSpaceOffset >= slotIndex->size);
		header->freeSpaceOffset -= slotIndex->size;

        // Properly decrement the number of slots post-deletion
        PageIndexSlot* offsets = (PageIndexSlot*)malloc(header->numSlots * sizeof(PageIndexSlot));
        memcpy(offsets, pageBuffer + PAGE_SIZE - _pageSlotOffset - (header->numSlots * sizeof(PageIndexSlot)), (header->numSlots * sizeof(PageIndexSlot)));
        int empty = 1;
        for (unsigned i = 1; i < header->numSlots; i++)
        {
            if (offsets[i].size == 0 && offsets[i].nextPage == 0)
            {
                empty++;
            }
            else
            {
                break;
            }
        }
        header->numSlots -= empty;

        // Write the header contents to the buffer, and then write that buffer to the page buffer so it can be pushed to disk
        memcpy((char*)headerBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), header, sizeof(CorePageIndexHeader));
        memcpy(pageBuffer + PAGE_SIZE - _pageSlotOffset, headerBuffer, _pageSlotOffset);

		// Zero out all of slotIndex
		slotIndex->pageOffset = 0;
	}
	// else we leave the pageOffset in order to facilitate later calls to reorganizePage()

	slotIndex->size = 0;
    slotIndex->nextPage = 0;
    slotIndex->nextSlot = 0;
    slotIndex->isAnchor = false;
    writePageIndexSlot(pageBuffer, rid.slotNum, _pageSlotOffset, slotIndex);

	// If need be, fix the freespace lists
	RC ret = movePageToCorrectFreeSpaceList(fileHandle, headerBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Write back the new page
	ret = fileHandle.writePage(rid.pageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	return rc::OK;
}

RC RecordBasedCoreManager::deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid)
{
	// Read the page of data RID points to
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

	// Recover the index header structure
    // PageIndexHeader* header = getPageIndexHeader(pageBuffer);
    char* headerBuffer = (char*)getPageIndexHeader(pageBuffer, _pageSlotOffset);
    CorePageIndexHeader *header = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
    memcpy(header, headerBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));

	// Find the slot where the record is stored
	PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum, _pageSlotOffset);

	// Has this record been deleted already?
	if (slotIndex->size == 0 && slotIndex->nextPage == 0)
	{
		// TODO: Should this be an error, deleting an already deleted record? Or do we allow it and skip the operation (like a free(NULL))
		return rc::RECORD_DELETED;
	}

    // Check to see if this is the end of a tombstone, since we should not delete it in that case
    if (slotIndex->isAnchor)
    {
        return rc::RECORD_IS_ANCHOR;
    }

	// If it exists, walk the tombstone chain and delete all records along the way
    if (slotIndex->nextPage > 0)
	{
		RID newRID, oldRID = rid;
        while (slotIndex->nextPage > 0) // walk the forward pointers
        {
			// Look ahead to the next record to visit
			newRID.pageNum = slotIndex->nextPage;
			newRID.slotNum = slotIndex->nextSlot;

			// Delete the current record
            // free(header);
            headerBuffer = (char*)getPageIndexHeader(pageBuffer, _pageSlotOffset);
            // header = (CorePageIndexHeader*)malloc(sizeof(CorePageIndexHeader));
            // memcpy(header, headerBuffer + (_pageSlotOffset - sizeof(CorePageIndexHeader)), sizeof(CorePageIndexHeader));
			ret = deleteRid(fileHandle, oldRID, slotIndex, headerBuffer, pageBuffer);
			if (ret != rc::OK)
			{
				return ret;
			}
			
            // Pull the new page into memory
            ret = fileHandle.readPage(newRID.pageNum, pageBuffer);
            if (ret != rc::OK)
            {
                return ret;
            }
            slotIndex = getPageIndexSlot(pageBuffer, newRID.slotNum, _pageSlotOffset);

			oldRID = newRID;
        }
	}
	else
	{
		// We are deleting the record and no tombstone chain
		ret = deleteRid(fileHandle, rid, slotIndex, header, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Write out the updated page data
		ret = fileHandle.writePage(rid.pageNum, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	return ret;
}

PFHeader::PFHeader()
{
    memset(freespaceLists, 0, sizeof(FreeSpaceList) * NUM_FREESPACE_LISTS);
}

// The cutoffs for the free space lists will end up like this:
//  [0] =    0
//  [1] =   32
//  [2] =  288
//  [3] =  544
//  [4] =  800
//  [5] = 1056
//  [6] = 1312
//  [7] = 1568
//  [8] = 1824
//  [9] = 2080
// [10] = 2336
void PFHeader::init()
{
    // Store constants so we are sure we're working with formats of files we expect
    headerSize = sizeof(PFHeader);
    pageSize = PAGE_SIZE;
    version = CURRENT_PF_VERSION;
    numPages = 0;
    numFreespaceLists = NUM_FREESPACE_LISTS;

    // Divide the freespace lists evenly, except for the first and last
    unsigned short cutoffDelta = PAGE_SIZE / (NUM_FREESPACE_LISTS + 5);
    unsigned short cutoff = cutoffDelta / 8; // Anything under this value is considered a 'full' page

    // the 0th index will have pages that are esentially considered compeltely full
    freespaceLists[0].listHead = 0;
    freespaceLists[0].cutoff = 0;

    // Each element starts a linked list of pages, which is garunteed to have at least the cutoff value of bytes free
    // So elements in the largest index will have the most amount of bytes free
    for (int i=1; i<NUM_FREESPACE_LISTS; ++i)
    {
        freespaceLists[i].listHead = 0;
        freespaceLists[i].cutoff = cutoff;
        cutoff += cutoffDelta;
    }
}

RC PFHeader::validate()
{
    if (headerSize != sizeof(PFHeader))
        return rc::HEADER_SIZE_CORRUPT;
 
    if (pageSize != PAGE_SIZE)
        return rc::HEADER_PAGESIZE_MISMATCH;
 
    if (version != CURRENT_PF_VERSION)
        return rc::HEADER_VERSION_MISMATCH;
 
    if (numFreespaceLists != NUM_FREESPACE_LISTS)
        return rc::HEADER_FREESPACE_LISTS_MISMATCH;
 
    return rc::OK;
}

PageIndexSlot* RecordBasedCoreManager::getPageIndexSlot(void* pageBuffer, unsigned slotNum, unsigned pageSlotOffset)
{
	return (PageIndexSlot*)((char*)pageBuffer + PAGE_SIZE - pageSlotOffset - ((slotNum + 1) * sizeof(PageIndexSlot)));
}

void RecordBasedCoreManager::writePageIndexSlot(void* pageBuffer, unsigned slotNum, unsigned pageSlotOffset, PageIndexSlot* slot)
{
    memcpy((char*)pageBuffer + PAGE_SIZE - pageSlotOffset - ((slotNum + 1) * sizeof(PageIndexSlot)), slot, sizeof(PageIndexSlot));   
}

void* RecordBasedCoreManager::getPageIndexHeader(void* pageBuffer, unsigned pageSlotOffset)
{
	return (void*)((char*)pageBuffer + PAGE_SIZE - pageSlotOffset);
}

int RecordBasedCoreManager::calculateFreespace(unsigned freespaceOffset, unsigned numSlots, unsigned pageSlotOffset)
{
	return PAGE_SIZE - freespaceOffset - pageSlotOffset - (numSlots * sizeof(PageIndexSlot));
}

unsigned Attribute::sizeInBytes(AttrType type, const void* value)
{
    switch(type)
    {
    case TypeInt:
		return sizeof(int);

    case TypeReal:
        return sizeof(float);

    case TypeVarChar:
		return sizeof(unsigned) + sizeof(char) * ( *(unsigned*)value );

    default:
        return 0;
    }
}

RC Attribute::allocateValue(AttrType attributeType, const void* valueIn, void** valueOut)
{
    if(*valueOut)
    {
        free(*valueOut);
    }

    unsigned attributeSize = Attribute::sizeInBytes(attributeType, valueIn);
    if (attributeSize == 0)
    {
        return rc::ATTRIBUTE_INVALID_TYPE;
    }

    *valueOut = malloc(attributeSize);
    if (!(*valueOut))
    {
        return rc::OUT_OF_MEMORY;
    }

    memcpy(*valueOut, valueIn, attributeSize);

    return rc::OK;
}
