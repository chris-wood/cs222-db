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

RC RecordBasedCoreManager::generateRecordHeader(const vector<Attribute> &recordDescriptor, const void *data, unsigned*& recHeader, unsigned& recLength, unsigned& recHeaderSize)
{
    // Compute the size of the record to be inserted
    // The header includes a field storing the number of attributes on disk, one offset field for each attribute, 
    // and then a final offset field to point to the end of the record (to enable easy size calculations)
    recHeaderSize = sizeof(unsigned) * recordDescriptor.size() + (2 * sizeof(unsigned));
    recLength = recHeaderSize;
    unsigned headerIndex = 0;
    unsigned dataOffset = 0;

    // Allocate an array of offets with N entries, where N is the number of fields as indicated
    // by the recordDescriptor vector. Each entry i in this array points to the address offset,
    // from the base address of the record on disk, where the i-th field is stored. 
    recHeader = (unsigned*)malloc(recHeaderSize);
    if (!recHeader)
    {
        return rc::OUT_OF_MEMORY;
    }

    // Write out the number of attributes as part of the header of the record
    unsigned numAttributes = recordDescriptor.size();
    memcpy(recHeader, &numAttributes, sizeof(unsigned));
    ++headerIndex;

    // Compute the compact record length and values to be inserted into the offset array
    for (vector<Attribute>::const_iterator itr = recordDescriptor.begin(); itr != recordDescriptor.end(); itr++)
    {
        // First, store the offset
        recHeader[headerIndex++] = recLength;

        // Now bump the length as needed based on the length of the contents
        const Attribute& attr = *itr;
        unsigned attrSize = Attribute::sizeInBytes(attr.type, (char*)data + dataOffset);
        if (attrSize == 0)
        {
            return rc::ATTRIBUTE_INVALID_TYPE;
        }

        recLength += attrSize;
        dataOffset += attrSize;
    }

    // Drop in the pointer (offset) to the end of the record
    recHeader[headerIndex++] = recLength;

    return rc::OK;
}

RC RecordBasedCoreManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) 
{
    unsigned recLength = 0;
    unsigned recHeaderSize = 0;
    unsigned* recHeader = NULL;
    RC ret = generateRecordHeader(recordDescriptor, data, recHeader, recLength, recHeaderSize);
    if (ret != rc::OK)
    {
        free(recHeader);
        return ret;
    }
    
    // Find the first page(s) with enough free space to hold this record
    PageNum pageNum;
    ret = findFreeSpace(fileHandle, recLength + sizeof(PageIndexSlot), pageNum);
    if (ret != rc::OK)
    {
        free(recHeader);
        return ret;
    }

    return insertRecordToPage(fileHandle, recordDescriptor, data, pageNum, rid);
}

RC RecordBasedCoreManager::insertRecordInplace(const vector<Attribute> &recordDescriptor, const void *data, PageNum pageNum, void* pageBuffer, RID &rid)
{
    // Recover the index header structure
    CorePageIndexFooter* footer = getCorePageIndexFooter(pageBuffer);

    // Verify this page has enough space for the record
    unsigned recLength = 0;
    unsigned recHeaderSize = 0;
    unsigned* recHeader = NULL;
    RC ret = generateRecordHeader(recordDescriptor, data, recHeader, recLength, recHeaderSize);
    if (ret != rc::OK)
    {
        free(recHeader);
        return ret;
    }

    unsigned freespace = calculateFreespace(footer->freeSpaceOffset, footer->numSlots);
    if (recLength > freespace)
    {
        return rc::RECORD_EXCEEDS_PAGE_SIZE;
    }

    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedCoreManager::insertRecord: header.freeSpaceOffset = " << footer->freeSpaceOffset << "\n";

    // Write the offsets array and data to disk
    memcpy((char*)pageBuffer + footer->freeSpaceOffset, recHeader, recHeaderSize);
    memcpy((char*)pageBuffer + footer->freeSpaceOffset + recHeaderSize, data, recLength - recHeaderSize);
    free(recHeader);

    // Create a new index slot entry and prepend it to the list
    PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, footer->numSlots);
    slotIndex->size = recLength;
    slotIndex->pageOffset = footer->freeSpaceOffset;
    slotIndex->nextPage = 0; // NULL
    slotIndex->nextSlot = 0; // NULL
    slotIndex->isAnchor = false; // only true if we're the end - anchor - of a tombstone chain

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedCoreManager::insertRecord: RID = (" << pageNum << ", " << footer->numSlots << ")\n";
    dbg::out << "RecordBasedCoreManager::insertRecord: Writing to: " << PAGE_SIZE - sizeof(CorePageIndexFooter) - ((footer->numSlots + 1) * sizeof(PageIndexSlot)) << "\n";
    dbg::out << "RecordBasedCoreManager::insertRecord: Offset: " << slotIndex->pageOffset << "\n";
    dbg::out << "RecordBasedCoreManager::insertRecord: Size of data: " << recLength << "\n";
    dbg::out << "RecordBasedCoreManager::insertRecord: Header free after record: " << (footer->freeSpaceOffset + recLength) << "\n";

    // Update the header information
    footer->numSlots++;
    footer->freeSpaceOffset += recLength;

    // Store the RID information and return
    rid.pageNum = pageNum;
    rid.slotNum = footer->numSlots - 1;

    return rc::OK;
}

RC RecordBasedCoreManager::insertRecordToPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, PageNum pageNum, RID &rid) 
{
    // Read in the designated page
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Write out the record to our pageBuffer
    ret = insertRecordInplace(recordDescriptor, data, pageNum, pageBuffer, rid);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Update the position of this page in the freespace lists, if necessary
    ret = movePageToCorrectFreeSpaceList(fileHandle, getCorePageIndexFooter(pageBuffer));
    if (ret != rc::OK)
    {
        return ret;
    }

    // Write the new page information to disk
    ret = fileHandle.writePage(pageNum, pageBuffer);
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
    ret = readHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

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
    memset(newPages, 0, PAGE_SIZE * requiredPages);
	memset(listSwapBuffer, 0, PAGE_SIZE);

    for (unsigned i=0; i<requiredPages; ++i)
    {
		unsigned char* newPage = i*PAGE_SIZE + newPages;

        // Create the blank index structure on each page (at the end)
        // Note: there are no slots yet, so there are no slotOffsets prepended to this struct on disk
        CorePageIndexFooter *index = getCorePageIndexFooter(newPage);

        index->freeSpaceOffset = 0;
        index->numSlots = 0;
        index->gapSize = 0;
        index->pageNumber = fileHandle.getNumberOfPages();
        index->freespaceList = header.numFreespaceLists - 1; // This is an empty page right now, put it in the largest slot
		index->freespacePrevPage = 0;
        index->freespaceNextPage = 0;

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
            CorePageIndexFooter *previousListFoot = getCorePageIndexFooter(listSwapBuffer);
            previousListFoot->freespacePrevPage = index->pageNumber;

            ret = fileHandle.writePage(previousListFoot->pageNumber, listSwapBuffer);
            if (ret != rc::OK)
            {
                free(newPages);
                return ret;
            }

            // Update the header info so the list points to the newly created page as the head
            oldFreeSpaceList.listHead = index->pageNumber;
            index->freespaceNextPage = previousListFoot->pageNumber;
        }

        // Append this new page to the file
        ret = fileHandle.appendPage(newPage);
		if (ret != rc::OK)
        {
			free(newPages);
            return ret;
        }
    }

	free(newPages);

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

RC RecordBasedCoreManager::movePageToCorrectFreeSpaceList(FileHandle &fileHandle, void* pageFooterBuffer)
{
    PFHeader header;
    RC ret = readHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

	CorePageIndexFooter* pageFooter = (CorePageIndexFooter*)pageFooterBuffer;
    unsigned freespace = calculateFreespace(pageFooter->freeSpaceOffset, pageFooter->numSlots);
    for (unsigned i=header.numFreespaceLists; i>0; --i)
    {
        unsigned listIndex = i - 1;
        if (freespace >= header.freespaceLists[listIndex].cutoff)
        {
            return movePageToFreeSpaceList(fileHandle, pageFooter, listIndex);
        }
    }

	// Backup plan, if we somehow ruined our page somewhow
	return movePageToFreeSpaceList(fileHandle, pageFooter, 0);
}

RC RecordBasedCoreManager::movePageToFreeSpaceList(FileHandle& fileHandle, void* pageFooterBuffer, unsigned destinationListIndex)
{
    CorePageIndexFooter *pageFooter = (CorePageIndexFooter*)pageFooterBuffer;
    if (destinationListIndex == pageFooter->freespaceList)
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
    if (pageFooter->freespacePrevPage > 0)
    {
        // Read in the next page
        ret = fileHandle.readPage(pageFooter->freespacePrevPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the prev pointer of the next page to our prev pointer
        CorePageIndexFooter *prevPageFooter = getCorePageIndexFooter(pageBuffer);
        prevPageFooter->freespaceNextPage = pageFooter->freespaceNextPage;

        ret = fileHandle.writePage(prevPageFooter->pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }
    else
    {
        // We were the beginning of the list, update the head pointer in the file header
        header.freespaceLists[pageFooter->freespaceList].listHead = pageFooter->freespaceNextPage;
    }

    // Update nextPage to point to our prevPage
    if (pageFooter->freespaceNextPage > 0)
    {
        // Read in the next page

        ret = fileHandle.readPage(pageFooter->freespaceNextPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the prev pointer of the next page to our prev pointer
        CorePageIndexFooter *nextPageFooter = getCorePageIndexFooter(pageBuffer);
        nextPageFooter->freespacePrevPage = pageFooter->freespacePrevPage;

        ret = fileHandle.writePage(nextPageFooter->pageNumber, pageBuffer);
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
        CorePageIndexFooter *listFirstPageFooter = getCorePageIndexFooter(pageBuffer);
        listFirstPageFooter->freespacePrevPage = pageFooter->pageNumber;

        ret = fileHandle.writePage(listFirstPageFooter->pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }

    // The next page for us is whatever was the previous 1st page
    pageFooter->freespaceNextPage = destinationList.listHead;

    // Update the header's 1st page to our page number
    destinationList.listHead = pageFooter->pageNumber;
    pageFooter->freespacePrevPage = 0;

    // Update our freespace index to the new list
    pageFooter->freespaceList = destinationListIndex;

    // Write out the header to disk with new data
    ret = writeHeader(fileHandle, &header);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::OK;
}

// Assume the rid does not change after update
RC RecordBasedCoreManager::updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid)
{
    // Read the page of data RID points to
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Pull the target slot into memory
    CorePageIndexFooter* realFooter = getCorePageIndexFooter(pageBuffer);
    PageIndexSlot* realSlot = getPageIndexSlot(pageBuffer, rid.slotNum);

    // Create the record
    unsigned recLength = 0;
    unsigned recHeaderSize = 0;
    unsigned* recHeader = NULL;
    ret = generateRecordHeader(recordDescriptor, data, recHeader, recLength, recHeaderSize);
    if (ret != rc::OK)
    {
        free(recHeader);
        return ret;
    }

    // Determine adjacent freespace on this page
    unsigned samePageFreeSpace = 0;
    bool expandInPlace = false;
    if (rid.slotNum == realFooter->numSlots - 1)
    {
        samePageFreeSpace = calculateFreespace(realFooter->freeSpaceOffset, realFooter->numSlots, _pageSlotOffset);
        if (samePageFreeSpace >= recLength)
        {
            expandInPlace = true;
        }
    }
    else
    {
        PageIndexSlot* nextSlot = getPageIndexSlot(pageBuffer, rid.slotNum + 1);
        samePageFreeSpace = nextSlot->pageOffset - realSlot->pageOffset;
        if (samePageFreeSpace >= recLength)
        {
            expandInPlace = true;
        }
    }

    // Check if deleted or on the next page
    if (realSlot->size == 0 && realSlot->nextPage == 0)
    {
        return rc::RECORD_DELETED;
    }

    // We can fit on the page we're currently at, which is the end of the tombstone chain
    if (expandInPlace) 
    {
        // Read the page of data RID points to
        unsigned char pageBuffer[PAGE_SIZE] = {0};
        RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
        realFooter = getCorePageIndexFooter(pageBuffer);
        realSlot = getPageIndexSlot(pageBuffer, rid.slotNum);

        // Copy the data to the same place in memory
        assert(realSlot->pageOffset + recLength < (PAGE_SIZE - _pageSlotOffset - (realFooter->numSlots * sizeof(PageIndexSlot))));
        memcpy(pageBuffer + realSlot->pageOffset, recHeader, recHeaderSize);
        memcpy(pageBuffer + realSlot->pageOffset + recHeaderSize, data, recLength - recHeaderSize);

        // Update the freespace offset in the header, as well as the index slot
        if (rid.slotNum == realFooter->numSlots - 1)
        {
            realFooter->freeSpaceOffset = realSlot->pageOffset + recLength;    
        }
        realSlot->size = recLength;

        // Delete the old slot, if necessary
        if (realSlot->nextPage > 0)
        {
            RID oldStone;
            oldStone.pageNum = realSlot->nextPage;
            oldStone.slotNum = realSlot->nextSlot;
            ret = deleteRecord(fileHandle, recordDescriptor, oldStone);
            if (ret != rc::OK)
            {
                return ret;
            }
        }
        realSlot->nextPage = 0;
        realSlot->nextSlot = 0;

        // Push the changes to disk
        writePageIndexSlot(pageBuffer, rid.slotNum, realSlot);
        ret = movePageToCorrectFreeSpaceList(fileHandle, realFooter);
        if (ret != rc::OK)
        {
            return ret;
        }
        ret = fileHandle.writePage(rid.pageNum, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }
    else // overflow onto new page somewhere and continue the tombstone chain
    {
        RID newRid;
        ret = insertRecord(fileHandle, recordDescriptor, data, newRid);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Re-read the page into memory, since we can potentially be on the same page
        ret = fileHandle.readPage(rid.pageNum, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Increase the gap size accordingly (the size of the previously stored record)
        realFooter = getCorePageIndexFooter(pageBuffer);
        realSlot = getPageIndexSlot(pageBuffer, rid.slotNum);
        realFooter->gapSize += realSlot->size;

        // Delete the old slot, if necessary
        if (realSlot->nextPage > 0)
        {
            RID oldStone;
            oldStone.pageNum = realSlot->nextPage;
            oldStone.slotNum = realSlot->nextSlot;
            ret = deleteRecord(fileHandle, recordDescriptor, oldStone);
            if (ret != rc::OK)
            {
                return ret;
            }
        }

        // Update the old page with a forward pointer and more free space
        realSlot->size = 0; // tombstones have a size of "0"
        realSlot->nextPage = newRid.pageNum;
        realSlot->nextSlot = newRid.slotNum;
        writePageIndexSlot(pageBuffer, rid.slotNum, realSlot);

        // Write the updated page & header information, for the old page, to disk
        ret = fileHandle.writePage(rid.pageNum, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Finally, check for reorganization
        if (realFooter->gapSize > REORG_THRESHOLD)
        {
            RC ret = reorganizePage(fileHandle, recordDescriptor, rid.pageNum);
            if (ret != rc::OK && ret != rc::PAGE_CANNOT_BE_ORGANIZED)
            {
                return ret;
            }
        }

        // // Re-read the page into memory, since we can potentially be on the same page
        ret = fileHandle.readPage(newRid.pageNum, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
        realFooter = getCorePageIndexFooter(pageBuffer);
        realSlot = getPageIndexSlot(pageBuffer, newRid.slotNum);
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

    return readRecord(fileHandle, recordDescriptor, rid, data, pageBuffer);
}

RC RecordBasedCoreManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data, void* pageBuffer)
{    
    RC ret = rc::OK;
    CorePageIndexFooter* footer = getCorePageIndexFooter(pageBuffer);
    if (footer->pageNumber != rid.pageNum)
    {
        return rc::PAGE_NUM_INVALID;
    }

    // Find the slot where the record is stored, walking tombstone chain if needed - O(1)
	PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);
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
        slotIndex = getPageIndexSlot(pageBuffer, nextSlot);
    }

    // Copy the contents of the record into the data block - O(1)
    int fieldOffset = (recordDescriptor.size() * sizeof(unsigned)) + (2 * sizeof(unsigned));
    memcpy(data, (char*)pageBuffer + slotIndex->pageOffset + fieldOffset, slotIndex->size - fieldOffset);

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
	PFHeader header;
	RC ret = readHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Rewrite freespace list data in the header
	for (unsigned i=0; i<header.numFreespaceLists-1; ++i)
	{
		header.freespaceLists[i].listHead = 0;
	}

	// All pages will be hanging off page 1, so point to page 1
	header.freespaceLists[header.numFreespaceLists-1].listHead = 1;

	// Write the header page back out to disk
	ret = writeHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

	// O(N) cost - We are rewriting all pages in this file
	unsigned char pageBuffer[PAGE_SIZE] = {0};
	for (unsigned page = 1; page <= header.numPages; ++page)
	{
        ret = fileHandle.readPage(page, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

		// Clear out all data on this page
		memset(pageBuffer, 0, PAGE_SIZE);

		// Rewrite the footer with new data
		CorePageIndexFooter *pageFooter = getCorePageIndexFooter(pageBuffer);
        pageFooter->gapSize = 0;
		pageFooter->numSlots = 0;
		pageFooter->freeSpaceOffset = 0;
		pageFooter->pageNumber = page;

		// Place all pages on the 'full' freepsace list, all sequentially linked to each other
		pageFooter->freespacePrevPage = page - 1;
		pageFooter->freespaceNextPage = (page < header.numPages) ? (page + 1) : 0;
		pageFooter->freespaceList = header.numFreespaceLists - 1;

		// Write the page back out to disk
		ret = fileHandle.writePage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	return rc::OK;
}

RC RecordBasedCoreManager::deleteRid(FileHandle& fileHandle, const RID& rid, PageIndexSlot* slotIndex, void* pageFooterBuffer, unsigned char* pageBuffer)
{
    // TODO: fetch the number of slots here
	CorePageIndexFooter* footer = (CorePageIndexFooter*)pageFooterBuffer;

	// Overwrite the memory of the record (not absolutely nessecary, but useful for finding bugs if we accidentally try to use it)
    assert(slotIndex->pageOffset + slotIndex->size < (PAGE_SIZE - _pageSlotOffset - (footer->numSlots * sizeof(PageIndexSlot))));
	memset(pageBuffer + slotIndex->pageOffset, 0, slotIndex->size);

	// If this is the last record on the page being deleted, merge the freespace with the main pool
	if (rid.slotNum + 1 == footer->numSlots)
	{
		// Update the header to merge the freespace pool with this deleted record (and others that are outstanding)
        assert(footer->freeSpaceOffset >= slotIndex->size);
		footer->freeSpaceOffset -= slotIndex->size;

        // Properly decrement the number of slots post-deletion
        PageIndexSlot* offsets = (PageIndexSlot*)malloc(footer->numSlots * sizeof(PageIndexSlot));
        memcpy(offsets, pageBuffer + PAGE_SIZE - _pageSlotOffset - (footer->numSlots * sizeof(PageIndexSlot)), (footer->numSlots * sizeof(PageIndexSlot)));
        int empty = 1;
        for (unsigned i = 1; i < footer->numSlots; i++)
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
        footer->numSlots -= empty;

		// Zero out all of slotIndex
		slotIndex->pageOffset = 0;
	}
	// else we leave the pageOffset in order to facilitate later calls to reorganizePage()

	slotIndex->size = 0;
    slotIndex->nextPage = 0;
    slotIndex->nextSlot = 0;
    slotIndex->isAnchor = false;
    writePageIndexSlot(pageBuffer, rid.slotNum, slotIndex);

	// If need be, fix the freespace lists
	RC ret = movePageToCorrectFreeSpaceList(fileHandle, pageFooterBuffer);
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
    CorePageIndexFooter* footer = getCorePageIndexFooter(pageBuffer);

	// Find the slot where the record is stored
	PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);

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
			ret = deleteRid(fileHandle, oldRID, slotIndex, footer, pageBuffer);
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
            slotIndex = getPageIndexSlot(pageBuffer, newRID.slotNum);

			oldRID = newRID;
        }
	}
	else
	{
		// We are deleting the record and no tombstone chain
		ret = deleteRid(fileHandle, rid, slotIndex, footer, pageBuffer);
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

void* RecordBasedCoreManager::getPageIndexFooter(void* pageBuffer, unsigned pageSlotOffset)
{
	return (void*)((char*)pageBuffer + PAGE_SIZE - pageSlotOffset);
}

void RecordBasedCoreManager::writePageIndexFooter(void* pageBuffer, void* footerBuffer, unsigned pageSlotOffset)
{
    memcpy((char*)pageBuffer + PAGE_SIZE - pageSlotOffset, footerBuffer, pageSlotOffset);
}

unsigned RecordBasedCoreManager::calculateFreespace(unsigned freespaceOffset, unsigned numSlots, unsigned pageSlotOffset)
{
	const unsigned slotSize = numSlots * sizeof(PageIndexSlot);
	unsigned freespace = PAGE_SIZE;
	
	assert(freespaceOffset <= freespace);
	freespace -= freespaceOffset;

	assert(pageSlotOffset <= freespace);
	freespace -= pageSlotOffset;

	assert(slotSize <= freespace);
	freespace -= slotSize;

	return freespace;
}

unsigned RecordBasedCoreManager::calcRecordSize(unsigned char* recordBuffer)
{
    unsigned numFields = 0;
    memcpy(&numFields, recordBuffer, sizeof(unsigned));
    unsigned recStart, recEnd;
    memcpy(&recStart, recordBuffer + sizeof(unsigned), sizeof(unsigned));
    memcpy(&recEnd, recordBuffer + sizeof(unsigned) + (numFields * sizeof(unsigned)), sizeof(unsigned));
    return (recEnd - recStart + (numFields * sizeof(unsigned)) + (2 * sizeof(unsigned))); 
}

CorePageIndexFooter* RecordBasedCoreManager::getCorePageIndexFooter(void* pageBuffer)
{
	return (CorePageIndexFooter*)getPageIndexFooter(pageBuffer, _pageSlotOffset);
}

PageIndexSlot* RecordBasedCoreManager::getPageIndexSlot(void* pageBuffer, unsigned slotNum)
{
	return getPageIndexSlot(pageBuffer, slotNum, _pageSlotOffset);
}

void RecordBasedCoreManager::writePageIndexSlot(void* pageBuffer, unsigned slotNum, PageIndexSlot* slot)
{
	return writePageIndexSlot(pageBuffer, slotNum, _pageSlotOffset, slot);
}

void RecordBasedCoreManager::writePageIndexFooter(void* pageBuffer, void* footerBuffer)
{
	return writePageIndexFooter(pageBuffer, footerBuffer, _pageSlotOffset);
}

unsigned RecordBasedCoreManager::calculateFreespace(unsigned freespaceOffset, unsigned numSlots)
{
	return calculateFreespace(freespaceOffset, numSlots, _pageSlotOffset);
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
