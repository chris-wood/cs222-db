#include "../util/returncodes.h"
#include "rbfm.h"

#include <assert.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <new>

#ifdef REDIRECT_PRINT_RECORD
#include <fstream>
#endif

RecordBasedFileManager* RecordBasedFileManager::_rbf_manager = 0;

RecordBasedFileManager* RecordBasedFileManager::instance()
{
    if(!_rbf_manager)
        _rbf_manager = new RecordBasedFileManager();

    return _rbf_manager;
}

RecordBasedFileManager::RecordBasedFileManager()
    : _pfm(*PagedFileManager::instance())
{
}

RecordBasedFileManager::~RecordBasedFileManager()
{
    // We don't want our static pointer to be pointing to deleted data in case the object is ever deleted!
	_rbf_manager = NULL;
}

RC RecordBasedFileManager::createFile(const string &fileName) {
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

RC RecordBasedFileManager::destroyFile(const string &fileName) {
    return _pfm.destroyFile(fileName.c_str());
}

RC RecordBasedFileManager::openFile(const string &fileName, FileHandle &fileHandle) {
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

RC RecordBasedFileManager::closeFile(FileHandle &fileHandle) {
    RC ret = _pfm.closeFile(fileHandle);
    if (ret != rc::OK)
    {
        return ret;
    }
    return rc::OK;
}

// Find a page (or insert a new one) that has at least 'bytes' free on it 
RC RecordBasedFileManager::findFreeSpace(FileHandle &fileHandle, unsigned bytes, PageNum& pageNum)
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
	
	// Zero out memory for cleanliness
    memset(newPages, 255, PAGE_SIZE * requiredPages);
	memset(listSwapBuffer, 255, PAGE_SIZE);

    for (unsigned i=0; i<requiredPages; ++i)
    {
        // Create the blank index structure on each page (at the end)
        // Note: there are no slots yet, so there are no slotOffsets prepended to this struct on disk
        PageIndexHeader* index = getPageIndexHeader(newPages);
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
            PageIndexHeader* previousListHead = getPageIndexHeader(listSwapBuffer);
            previousListHead->prevPage = pageNum;
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

RC RecordBasedFileManager::movePageToCorrectFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader)
{
    PFHeader header;
    readHeader(fileHandle, &header);

    unsigned freespace = calculateFreespace(pageHeader.freeSpaceOffset, pageHeader.numSlots);
    for (unsigned i=header.numFreespaceLists; i>0; --i)
    {
        unsigned listIndex = i - 1;
        if (freespace >= header.freespaceLists[listIndex].cutoff)
        {
            return movePageToFreeSpaceList(fileHandle, pageHeader, listIndex);
        }
    }

    return rc::RECORD_EXCEEDS_PAGE_SIZE;
}

RC RecordBasedFileManager::movePageToFreeSpaceList(FileHandle& fileHandle, PageIndexHeader& pageHeader, unsigned destinationListIndex)
{
    if (destinationListIndex == pageHeader.freespaceList)
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
    if (pageHeader.prevPage > 0)
    {
        // Read in the next page
        ret = fileHandle.readPage(pageHeader.prevPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the prev pointer of the next page to our prev pointer
        PageIndexHeader* prevPageHeader = getPageIndexHeader(pageBuffer);
        prevPageHeader->nextPage = pageHeader.nextPage;
        ret = fileHandle.writePage(prevPageHeader->pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }
    else
    {
        // We were the beginning of the list, update the head pointer in the file header
        header.freespaceLists[pageHeader.freespaceList].listHead = pageHeader.nextPage;
    }

    // Update nextPage to point to our prevPage
    if (pageHeader.nextPage > 0)
    {
        // Read in the next page

        ret = fileHandle.readPage(pageHeader.nextPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the prev pointer of the next page to our prev pointer
        PageIndexHeader* nextPageHeader = getPageIndexHeader(pageBuffer);
        nextPageHeader->prevPage = pageHeader.prevPage;
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
        PageIndexHeader* listFirstPageHeader = getPageIndexHeader(pageBuffer);
        listFirstPageHeader->prevPage = pageHeader.pageNumber;
        ret = fileHandle.writePage(listFirstPageHeader->pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }

    // The next page for us is whatever was the previous 1st page
    pageHeader.nextPage = destinationList.listHead;

    // Update the header's 1st page to our page number
    destinationList.listHead = pageHeader.pageNumber;
    pageHeader.prevPage = 0;

    // Update our freespace index to the new list
    pageHeader.freespaceList = destinationListIndex;

    // Write out the header to disk with new data
    ret = writeHeader(fileHandle, &header);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::OK;
}

RC RecordBasedFileManager::writeHeader(FileHandle &fileHandle, PFHeader* header)
{
    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedFileManager::writeHeader(" << fileHandle.getFilename() << ")\n";

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

RC RecordBasedFileManager::readHeader(FileHandle &fileHandle, PFHeader* header)
{
    RC ret = rc::OK;
    unsigned char buffer[PAGE_SIZE] = {0};
    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedFileManager::readHeader(" << fileHandle.getFilename() << ")\n";

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

RC RecordBasedFileManager::generateRecordHeader(const vector<Attribute> &recordDescriptor, const void *data, unsigned*& recHeader, unsigned& recLength, unsigned& recHeaderSize)
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

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) 
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

    // Read in the designated page
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    ret = fileHandle.readPage(pageNum, pageBuffer);
	if (ret != rc::OK)
	{
        free(recHeader);
		return ret;
	}

    // Recover the index header structure
    PageIndexHeader* header = getPageIndexHeader(pageBuffer);

    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedFileManager::insertRecord: header.freeSpaceOffset = " << header->freeSpaceOffset << "\n";

    // Write the offsets array and data to disk
    memcpy(pageBuffer + header->freeSpaceOffset, recHeader, recHeaderSize);
    memcpy(pageBuffer + header->freeSpaceOffset + recHeaderSize, data, recLength - recHeaderSize);
    free(recHeader);

    // Create a new index slot entry and prepend it to the list
    PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, header->numSlots);
    slotIndex->size = recLength;
    slotIndex->pageOffset = header->freeSpaceOffset;
    slotIndex->nextPage = 0; // NULL
    slotIndex->nextSlot = 0; // NULL

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedFileManager::insertRecord: RID = (" << pageNum << ", " << header->numSlots << ")\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Writing to: " << PAGE_SIZE - sizeof(PageIndexHeader) - ((header->numSlots + 1) * sizeof(PageIndexSlot)) << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Offset: " << slotIndex->pageOffset << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Size of data: " << recLength << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Header free after record: " << (header->freeSpaceOffset + recLength) << "\n";

    // Update the header information
    header->numSlots++;
    header->freeSpaceOffset += recLength;

    // Update the position of this page in the freespace lists, if necessary
    ret = movePageToCorrectFreeSpaceList(fileHandle, *header);
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

    // Once the write is committed, store the RID information and return
    rid.pageNum = pageNum;
    rid.slotNum = header->numSlots - 1;

    return rc::OK;
}


RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) 
{    
    // Pull the page into memory - O(1)
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

    // Find the slot where the record is stored, walking tombstone chain if needed - O(1)
	PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);
	if (slotIndex->size == 0 && slotIndex->nextPage == 0) // if not a tombstone and empty, it was deleted
	{
		return rc::RECORD_DELETED;
	}
    else if (slotIndex->nextPage > 0 || slotIndex->nextSlot > 0) // check to see if we moved to a different page
    {
        unsigned char tempPageBuffer[PAGE_SIZE] = {0};
        while (slotIndex->nextPage > 0 || slotIndex->nextSlot > 0) // walk the forward pointers
        {
            ret = fileHandle.readPage(slotIndex->nextPage, tempPageBuffer);
            if (ret != rc::OK)
            {
                return ret;
            }
            slotIndex = getPageIndexSlot(tempPageBuffer, slotIndex->nextSlot);
            memcpy(pageBuffer, tempPageBuffer, PAGE_SIZE);
        }
    }

    // Copy the contents of the record into the data block - O(1)
    int fieldOffset = (recordDescriptor.size() * sizeof(unsigned)) + (2 * sizeof(unsigned));
    memcpy(data, pageBuffer + slotIndex->pageOffset + fieldOffset, slotIndex->size - fieldOffset);

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedFileManager::readRecord: RID = (" << rid.pageNum << ", " << rid.slotNum << ")\n";;
    dbg::out << "RecordBasedFileManager::readRecord: Reading from: " << PAGE_SIZE - sizeof(PageIndexHeader) - ((rid.slotNum + 1) * sizeof(PageIndexSlot)) << "\n";;
    dbg::out << "RecordBasedFileManager::readRecord: Offset: " << slotIndex->pageOffset << "\n";
    dbg::out << "RecordBasedFileManager::readRecord: Size: " << slotIndex->size << "\n";

    return rc::OK;
}

RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) 
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

RC RecordBasedFileManager::deleteRecords(FileHandle &fileHandle)
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
		PageIndexHeader* pageHeader = getPageIndexHeader(pageBuffer);
        pageHeader->gapSize = 0;
		pageHeader->numSlots = 0;
		pageHeader->freeSpaceOffset = 0;
		pageHeader->pageNumber = page;
		pageHeader->prevPage = page - 1;
		pageHeader->nextPage = (page < pfHeader->numPages) ? (page - 1) : 0;
		pageHeader->freespaceList = pfHeader->numFreespaceLists - 1;

		RC ret = fileHandle.writePage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	return rc::OK;
}

RC RecordBasedFileManager::deleteRid(FileHandle& fileHandle, const RID& rid, PageIndexSlot* slotIndex, PageIndexHeader* header, unsigned char* pageBuffer)
{
	// Overwrite the memory of the record (not absolutely nessecary, but useful for finding bugs if we accidentally try to use it)
    assert(slotIndex->pageOffset + slotIndex->size < (PAGE_SIZE - sizeof(PageIndexHeader) - (header->numSlots * sizeof(PageIndexSlot))));
	memset(pageBuffer + slotIndex->pageOffset, 255, slotIndex->size);

	// If this is the last record on the page being deleted, merge the freespace with the main pool
	if (rid.slotNum + 1 == header->numSlots)
	{
		// Update the header to merge the freespace pool with this deleted record
		header->freeSpaceOffset -= slotIndex->size;
		header->numSlots -= 1;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), header, sizeof(PageIndexHeader));

		// Zero out all of slotIndex
		slotIndex->pageOffset = 0;
	}
	// else we leave the pageOffset in order to facilitate later calls to reorganizePage()

	slotIndex->size = 0;
    slotIndex->nextPage = 0;
    slotIndex->nextSlot = 0;
    writePageIndexSlot(pageBuffer, rid.slotNum, slotIndex);

	// Write back the new page
	RC ret = fileHandle.writePage(rid.pageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	// If need be, fix the freespace lists
	return movePageToCorrectFreeSpaceList(fileHandle, *header); 
}

RC RecordBasedFileManager::deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid)
{
	// Read the page of data RID points to
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

	// Recover the index header structure
    PageIndexHeader* header = getPageIndexHeader(pageBuffer);

	// Find the slot where the record is stored
	PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);

	// Has this record been deleted already?
	if (slotIndex->size == 0 && slotIndex->nextPage == 0)
	{
		// TODO: Should this be an error, deleting an already deleted record? Or do we allow it and skip the operation (like a free(NULL))
		return rc::RECORD_DELETED;
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
			ret = deleteRid(fileHandle, oldRID, slotIndex, header, pageBuffer);
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
		ret = deleteRid(fileHandle, rid, slotIndex, header, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	return ret;
}

// Assume the rid does not change after update
RC RecordBasedFileManager::updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid)
{
	// Read the page of data RID points to
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Chain of RIDs that represents the tombstones on disk
    vector<RID> rids;
    vector<unsigned> adjacentFreeSpace;
    rids.push_back(rid);

    // Read in the header of this page to determine the currently available free space
    PageIndexHeader* realHeader = getPageIndexHeader(pageBuffer);
    if (rid.slotNum == realHeader->numSlots - 1)
    {
        PageIndexSlot* newSlotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);
        adjacentFreeSpace.push_back(realHeader->freeSpaceOffset - newSlotIndex->pageOffset);
    }
    else
    {
        PageIndexSlot* newSlotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);
        PageIndexSlot* nextSlotIndex = getPageIndexSlot(pageBuffer, rid.slotNum + 1);
        adjacentFreeSpace.push_back(nextSlotIndex->pageOffset - newSlotIndex->pageOffset);
    }

    // Find the slot where the record is stored - O(1)
    PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);
    if (slotIndex->size == 0 && slotIndex->nextPage == 0)
    {
        return rc::RECORD_DELETED;
    }
    else if (slotIndex->nextPage > 0) // check to see if we moved to a different page
    {
        unsigned char tempPageBuffer[PAGE_SIZE] = {0};
        while (slotIndex->nextPage != 0) // walk the forward pointers
        {
            // Append to the tombstone chain
            RID newRID;
            newRID.pageNum = slotIndex->nextPage;
            newRID.slotNum = slotIndex->nextSlot;
            rids.push_back(newRID);

            // Pull the new page into memory
            ret = fileHandle.readPage(newRID.pageNum, tempPageBuffer);
            if (ret != rc::OK)
            {
                return ret;
            }
            slotIndex = getPageIndexSlot(tempPageBuffer, slotIndex->nextSlot);

            // Update the adjacency information
            realHeader = getPageIndexHeader(tempPageBuffer);
            if (newRID.slotNum == realHeader->numSlots - 1)
            {
                PageIndexSlot* newSlotIndex = getPageIndexSlot(tempPageBuffer, newRID.slotNum);
                adjacentFreeSpace.push_back(realHeader->freeSpaceOffset - newSlotIndex->pageOffset);
                // adjacentFreeSpace.push_back((PAGE_SIZE - sizeof(PageIndexHeader) - (realHeader->numSlots * sizeof(PageIndexHeader))) - newSlotIndex->pageOffset);
            }
            else
            {
                PageIndexSlot* newSlotIndex = getPageIndexSlot(tempPageBuffer, newRID.slotNum);
                PageIndexSlot* nextSlotIndex = getPageIndexSlot(tempPageBuffer, newRID.slotNum + 1);
                adjacentFreeSpace.push_back(nextSlotIndex->pageOffset - newSlotIndex->pageOffset);
            }
        }
    }

    // Extract the exact location of the record
    PageNum realPage = rids[rids.size() - 1].pageNum;
    unsigned realSlotNum = rids[rids.size() - 1].slotNum;

    // With the correct page, now re-read everything into memory
    // Read the page of data RID points to
    memset(pageBuffer, 0, PAGE_SIZE);
    ret = fileHandle.readPage(realPage, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Pull the target slot into memory
    PageIndexSlot* realSlot = getPageIndexSlot(pageBuffer, realSlotNum);

    // Ensure that the record was not previously deleted
    if (realSlot->size == 0)
    {
        return rc::RECORD_DELETED;
    }

	unsigned recLength = 0;
	unsigned recHeaderSize = 0;
	unsigned* recHeader = NULL;
	ret = generateRecordHeader(recordDescriptor, data, recHeader, recLength, recHeaderSize);
	if (ret != rc::OK)
	{
		free(recHeader);
		return ret;
	}

    // Walk the tombstone chain and see if we can fit in any of these slots
    unsigned ti = 0;
    for (; ti < adjacentFreeSpace.size(); ti++)
    {
        if (recLength <= adjacentFreeSpace[ti])
        {
            break;
        }
    }

    // Put it on one of the previously allocated tombstones
    if (ti < adjacentFreeSpace.size())
    {
        PageNum placePage = rids[ti].pageNum;
        unsigned placeSlotNum = rids[ti].slotNum;

        // Read the page into memory
        ret = fileHandle.readPage(placePage, pageBuffer);
        if (ret != rc::OK)
        {
			free(recHeader);
            return ret;
        }

        // Update the header with the new gap size => difference between new shorter record and adjacent record
        PageIndexHeader* placeHeader = getPageIndexHeader(pageBuffer);
        placeHeader->gapSize += adjacentFreeSpace[ti] - recLength;

        dbg::out << dbg::LOG_EXTREMEDEBUG;
        dbg::out << "RecordBasedFileManager::updateRecord: Size of data: " << recLength << "\n";
        dbg::out << "RecordBasedFileManager::updateRecord: Updating gap size to: " << placeHeader->gapSize << "\n";

        // Pull out the index slot
        PageIndexSlot* placeSlot = getPageIndexSlot(pageBuffer, placeSlotNum);

        // Write the offsets array and data to disk
        assert(placeSlot->pageOffset + recLength < (PAGE_SIZE - sizeof(PageIndexHeader) - (placeHeader->numSlots * sizeof(PageIndexSlot))));
        memcpy(pageBuffer + placeSlot->pageOffset, recHeader, recHeaderSize);
        memcpy(pageBuffer + placeSlot->pageOffset + recHeaderSize, data, recLength - recHeaderSize);
        free(recHeader);

        // Update the slot information
        placeSlot->size = recLength;
        placeSlot->nextPage = 0;
        placeSlot->nextSlot = 0;
        writePageIndexSlot(pageBuffer, placeSlotNum, placeSlot);

        // Write the updated page header to the buffer
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), placeHeader, sizeof(PageIndexHeader));

        // Write the new page information to disk
        ret = fileHandle.writePage(placePage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Finally, check for reorganization
        if (placeHeader->gapSize > REORG_THRESHOLD)
        {
            RC ret = reorganizePage(fileHandle, recordDescriptor, placePage);
            if (ret != rc::OK)
            {
                return ret;
            }
        }

        // Now walk the remaining tombstones and set them to deleted
        // The gap size doesn't change for these records, since they were already deleted when the tombstones were created
        for (++ti; ti < adjacentFreeSpace.size(); ti++)
        {
            PageNum delPage = rids[ti].pageNum;
            unsigned delSlotNum = rids[ti].slotNum;

            // Read the page into memory
            ret = fileHandle.readPage(delPage, pageBuffer);
            if (ret != rc::OK)
            {
                return ret;
            }

            // Update the slot to mark the now tombstone as deleted so it will be squashed during reorganizePage
            PageIndexSlot* delSlot = getPageIndexSlot(pageBuffer, delSlotNum);
            delSlot->size = 0;
            delSlot->nextPage = 0;
            delSlot->nextSlot = 0;
            writePageIndexSlot(pageBuffer, delSlotNum, delSlot);

            // Write the new page information to disk
            ret = fileHandle.writePage(delPage, pageBuffer);
            if (ret != rc::OK)
            {
                return ret;
            }
        }
    }
    else // overflow onto new page somewhere and continue the tombstone chain
    {
        PageNum oldPage = rids[rids.size() - 1].pageNum;
        unsigned oldSlotNum = rids[rids.size() - 1].slotNum;

        // Read the page into memory
        ret = fileHandle.readPage(oldPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Pull out the index slot and header
        PageIndexHeader* oldHeader = getPageIndexHeader(pageBuffer);
        PageIndexSlot* oldSlot = getPageIndexSlot(pageBuffer, oldSlotNum);

        // Find the first page(s) with enough free space to hold this record
        PageNum newPageNum;
        RC ret = findFreeSpace(fileHandle, recLength + sizeof(PageIndexSlot), newPageNum);
        if (ret != rc::OK)
        {
            free(recHeader);
            return ret;
        }

        // Read in the designated page
        unsigned char newPageBuffer[PAGE_SIZE] = {0};
        ret = fileHandle.readPage(newPageNum, newPageBuffer);
        if (ret != rc::OK)
        {
            free(recHeader);
            return ret;
        }

        // Recover the index header structure
        PageIndexHeader* newHeader = getPageIndexHeader(newPageBuffer);

        // Increase the gap size accordingly (the size of the previously stored record)
        oldHeader->gapSize += oldSlot->size;

        dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedFileManager::updateRecord: header.freeSpaceOffset = " << newHeader->freeSpaceOffset << "\n";

        // Write the offsets array and data to the buffer
        assert(newHeader->freeSpaceOffset + recLength < (PAGE_SIZE - sizeof(PageIndexHeader) - (newHeader->numSlots * sizeof(PageIndexSlot))));
        memcpy(newPageBuffer + newHeader->freeSpaceOffset, recHeader, recHeaderSize);
        memcpy(newPageBuffer + newHeader->freeSpaceOffset + recHeaderSize, data, recLength - recHeaderSize);
        free(recHeader);

        // Create a new index slot entry and prepend it to the list
        PageIndexSlot* slotIndex = getPageIndexSlot(newPageBuffer, newHeader->numSlots);
        slotIndex->size = recLength;
        slotIndex->pageOffset = newHeader->freeSpaceOffset;
        slotIndex->nextPage = 0; // NULL
        slotIndex->nextSlot = 0; // NULL    
        writePageIndexSlot(newPageBuffer, newHeader->numSlots, slotIndex);

        dbg::out << dbg::LOG_EXTREMEDEBUG;
        dbg::out << "RecordBasedFileManager::updateRecord: RID = (" << newPageNum << ", " << newHeader->numSlots << ")\n";
        dbg::out << "RecordBasedFileManager::updateRecord: Writing to: " << PAGE_SIZE - sizeof(PageIndexHeader) - ((newHeader->numSlots + 1) * sizeof(PageIndexSlot)) << "\n";
        dbg::out << "RecordBasedFileManager::updateRecord: Offset: " << slotIndex->pageOffset << "\n";
        dbg::out << "RecordBasedFileManager::updateRecord: Size of data: " << recLength << "\n";
        dbg::out << "RecordBasedFileManager::updateRecord: Header free after record: " << (newHeader->freeSpaceOffset + recLength) << "\n";

        // Update the new page's header information
        newHeader->numSlots++;
        newHeader->freeSpaceOffset += recLength;
        memcpy(newPageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), newHeader, sizeof(PageIndexHeader));

        // Update the position of this page in the freespace lists, if necessary
        ret = movePageToCorrectFreeSpaceList(fileHandle, *newHeader);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Write the new page information to disk
        ret = fileHandle.writePage(newPageNum, newPageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Re-read the page into memory, since we can potentially be on the same page
        ret = fileHandle.readPage(oldPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the current page with a forward pointer and more free space
        oldSlot->size = 0; // tombstones have a size of "0"
        oldSlot->nextPage = newPageNum;
        oldSlot->nextSlot = newHeader->numSlots - 1;
        writePageIndexSlot(pageBuffer, oldSlotNum, oldSlot);

        // Write the updated page & header information, for the old page, to disk
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), oldHeader, sizeof(PageIndexHeader));
        ret = fileHandle.writePage(oldPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Finally, check for reorganization
        if (oldHeader->gapSize > REORG_THRESHOLD)
        {
            RC ret = reorganizePage(fileHandle, recordDescriptor, oldPage);
            if (ret != rc::OK)
            {
                return ret;
            }
        }
    }

	return rc::OK;
}

RC RecordBasedFileManager::readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data)
{
    // Pull the page into memory - O(1)
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Find the attribute index sought after by the caller
    int attrIndex = 1; // offset by 1 to start to skip over the #attributes slot in the record header
    Attribute attr;
    for (vector<Attribute>::const_iterator itr = recordDescriptor.begin(); itr != recordDescriptor.end(); itr++)
    {
        if (itr->name == attributeName)
        {
            attr = *itr;
            break;
        }
        attrIndex++;
    }

    // Find the slot where the record is stored - O(1)
    PageIndexSlot* slotIndex = (PageIndexSlot*)(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - ((rid.slotNum + 1) * sizeof(PageIndexSlot)));

    // Copy the contents of the record into the data block - O(1)
    unsigned char* tempBuffer = (unsigned char*)malloc(slotIndex->size);
    memcpy(tempBuffer, pageBuffer + slotIndex->pageOffset, slotIndex->size);

    // Determine the offset of the attribute sought after
    unsigned offset = 0;
    memcpy(&offset, tempBuffer + (attrIndex * sizeof(unsigned)), sizeof(unsigned));

    // Now read the data into the caller's buffer
    switch (attr.type)
    {
        case TypeInt:
        case TypeReal:
            memcpy(data, tempBuffer + offset, sizeof(unsigned));
            break;
        case TypeVarChar:
            int dataLen = 0;
            memcpy(&dataLen, tempBuffer + offset, sizeof(unsigned));
            memcpy(data, tempBuffer + offset + sizeof(unsigned), dataLen);
            break;
    }

    // Free up the memory
    free(tempBuffer);

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedFileManager::readAttribute: RID = (" << rid.pageNum << ", " << rid.slotNum << ")\n";;
    dbg::out << "RecordBasedFileManager::readAttribute: Reading from: " << PAGE_SIZE - sizeof(PageIndexHeader) - ((rid.slotNum + 1) * sizeof(PageIndexSlot)) << "\n";;
    dbg::out << "RecordBasedFileManager::readAttribute: Offset: " << slotIndex->pageOffset << "\n";
    dbg::out << "RecordBasedFileManager::readAttribute: Size: " << slotIndex->size << "\n";

    return rc::OK;
}

RC RecordBasedFileManager::reorganizeBufferedPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber, unsigned char* pageBuffer)
{
	RC ret = rc::OK;

	// Read in the page header and recover the number of slots
    PageIndexHeader header;
    memcpy(&header, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));

    // Only proceed if there are actually slots on this page that need to be reordered
    if (header.numSlots > 0)
    {
        unsigned reallocatedSpace = 0;

        // Read in the record offsets
        PageIndexSlot* offsets = (PageIndexSlot*)malloc(header.numSlots * sizeof(PageIndexSlot));
        memcpy(offsets, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - (header.numSlots * sizeof(PageIndexSlot)), (header.numSlots * sizeof(PageIndexSlot)));

        // Find the first non-empty slot
        int offsetIndex = -1;
        for (int i = header.numSlots - 1; i >= 0; i--)
        {
            if (offsets[i].size != 0 && offsets[i].nextPage == 0) // not deleted, not tombstone
            {
                offsetIndex = i;
                break;
            }
        }

        // Allocate space for the smaller page and move the first non-empty record to the front
        unsigned char newBuffer[PAGE_SIZE] = {0};

        // Shift the record down by computing its size
        unsigned offset = 0;
        memcpy(newBuffer + offset, pageBuffer + offsets[offsetIndex].pageOffset, offsets[offsetIndex].size); 
        offset += offsets[offsetIndex].size; 
        reallocatedSpace += offsets[offsetIndex].pageOffset;
        offsets[offsetIndex].pageOffset = 0; // we just moved to the start of the page
        offsets[offsetIndex].nextPage = 0;
        offsets[offsetIndex].nextSlot = 0;

        // Push everything down by looking ahead (walking the offset list in reverse order)
        while (offsetIndex > 0)
        {
            // Short circuit
            if (offsetIndex == 0)
            {
                break;
            }

            // Find the next non-empty slot to determine the shift difference
            int nextIndex;
            for (nextIndex = offsetIndex - 1; nextIndex >= 0; nextIndex--)
            {
                if (offsets[nextIndex].size != 0 && offsets[nextIndex].nextPage == 0) // not deleted, not tombstone
                {
                    reallocatedSpace += offsets[nextIndex].pageOffset - offset;
                    break;
                }
            }

            // Update the record's offset based on the previous entry's size and then calculate our own size
            unsigned pageOffset = offsets[nextIndex].pageOffset;
            offsets[nextIndex].pageOffset = offset;
            offsets[nextIndex].nextPage = 0;
            offsets[nextIndex].nextSlot = 0;

            // Shift to the front
            memcpy(newBuffer + offset, pageBuffer + pageOffset, offsets[nextIndex].size); 
            offset += offsets[nextIndex].size; 

            // Proceed to look at the next candidate record to shift
            offsetIndex = nextIndex;
        }

        // Update the freespace offset for future insertions
        header.freeSpaceOffset -= reallocatedSpace;
        header.gapSize -= reallocatedSpace;
        movePageToCorrectFreeSpaceList(fileHandle, header);

        // Update the slot entries in the new page
        memcpy(newBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - (header.numSlots * sizeof(PageIndexSlot)), offsets, (header.numSlots * sizeof(PageIndexSlot)));
        memcpy(newBuffer + PAGE_SIZE - sizeof(PageIndexHeader), &header, sizeof(PageIndexHeader));

        // Push the changes to disk
        ret = fileHandle.writePage(pageNumber, (void*)newBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }
    }
    else
    {
        return rc::PAGE_CANNOT_BE_ORGANIZED;   
    }

    return rc::OK;
}

RC RecordBasedFileManager::reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber)
{
    // Pull the page into memory - O(1)
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(pageNumber, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

	return reorganizeBufferedPage(fileHandle, recordDescriptor, pageNumber, pageBuffer);
}

RC RecordBasedFileManager::reorganizeFile(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor)
{
	PFHeader header;
	RC ret = readHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

	unsigned char pageBuffer[PAGE_SIZE] = {0};
	std::vector< unsigned > freespace;
	std::vector< std::vector<unsigned> > recordSizes;

	// Step 1) Reorganize individual pages
	for (unsigned page=0; page<header.numPages; ++page)
	{
		fileHandle.readPage(page, pageBuffer);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Reorganize the page to 
		reorganizeBufferedPage(fileHandle, recordDescriptor, page, pageBuffer);
		PageIndexHeader* pageIndexHeader = getPageIndexHeader(pageBuffer);

		// Keep track of freespace on each page
		freespace.push_back(calculateFreespace(pageIndexHeader->freeSpaceOffset, pageIndexHeader->numSlots));

		// Keep track of the sizes of all records on this page
		recordSizes.push_back(std::vector<unsigned>());
		std::vector<unsigned>& currentPageRecordSizes = recordSizes.back();
		for (unsigned slot=0; slot < pageIndexHeader->numSlots; ++slot)
		{
			PageIndexSlot* pageSlot = getPageIndexSlot(pageBuffer, slot);
			currentPageRecordSizes.push_back(pageSlot->size);
		}
	}

	// Step 2) Do other stuff

	return rc::OK;
}

RC RecordBasedFileManager::scan(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttributeString, const CompOp compOp, const void *value, const vector<string> &attributeNames, RBFM_ScanIterator &rbfm_ScanIterator)
{
	return rbfm_ScanIterator.init(fileHandle, recordDescriptor, conditionAttributeString, compOp, value, attributeNames, rbfm_ScanIterator);
}

unsigned RecordBasedFileManager::calcRecordSize(unsigned char* recordBuffer)
{
    unsigned numFields = 0;
    memcpy(&numFields, recordBuffer, sizeof(unsigned));
    unsigned recStart, recEnd;
    memcpy(&recStart, recordBuffer + sizeof(unsigned), sizeof(unsigned));
    memcpy(&recEnd, recordBuffer + sizeof(unsigned) + (numFields * sizeof(unsigned)), sizeof(unsigned));
    return (recEnd - recStart + (numFields * sizeof(unsigned)) + (2 * sizeof(unsigned))); 
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

PageIndexSlot* RecordBasedFileManager::getPageIndexSlot(void* pageBuffer, unsigned slotNum)
{
	return (PageIndexSlot*)((char*)pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - ((slotNum + 1) * sizeof(PageIndexSlot)));
}

void RecordBasedFileManager::writePageIndexSlot(void* pageBuffer, unsigned slotNum, PageIndexSlot* slot)
{
    memcpy((char*)pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - ((slotNum + 1) * sizeof(PageIndexSlot)), slot, sizeof(PageIndexSlot));   
}

PageIndexHeader* RecordBasedFileManager::getPageIndexHeader(void* pageBuffer)
{
	return (PageIndexHeader*)((char*)pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader));
}

unsigned RecordBasedFileManager::calculateFreespace(unsigned freespaceOffset, unsigned numSlots)
{
	return PAGE_SIZE - freespaceOffset - sizeof(PageIndexHeader) - (numSlots * sizeof(PageIndexSlot));
}

RC RBFM_ScanIterator::findAttributeByName(const vector<Attribute>& recordDescriptor, const string& conditionAttribute, unsigned& index)
{
	// Linearly search through the attributes to match up the name
	index = 0;
	for(vector<Attribute>::const_iterator it = recordDescriptor.begin(); it != recordDescriptor.end(); ++it, ++index)
	{
		if (it->name == conditionAttribute)
		{
			return rc::OK;
		}
	}

	return rc::ATTRIBUTE_NOT_FOUND;
}

RC RBFM_ScanIterator::init(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttributeString, const CompOp compOp, const void *value, const vector<string> &attributeNames, RBFM_ScanIterator &rbfm_ScanIterator)
{
	RC ret = rc::OK;
	
	// We only care about the condition attribute if we are actually comparing things
	if (compOp != NO_OP)
	{
		findAttributeByName(recordDescriptor, conditionAttributeString, _conditionAttributeIndex);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	// Save data we will need for future comparasion operations
	_fileHandle = &fileHandle;
	_nextRid.pageNum = 1;
	_nextRid.slotNum = 0;
	_comparasionOp = compOp;

	if (compOp != NO_OP)
	{
		_conditionAttributeType = recordDescriptor[_conditionAttributeIndex].type;
		allocateValue(_conditionAttributeType, value);
	}

	_returnAttributeIndices.clear();
	_returnAttributeTypes.clear();
	for (vector<string>::const_iterator it = attributeNames.begin(); it != attributeNames.end(); ++it)
	{
		unsigned index;
		ret = findAttributeByName(recordDescriptor, *it, index);
		if (ret != rc::OK)
		{
			return ret;
		}

		_returnAttributeIndices.push_back(index);
		_returnAttributeTypes.push_back( recordDescriptor[index].type );
	}

	return rc::OK;
}

void RBFM_ScanIterator::nextRecord(unsigned numSlots)
{
	_nextRid.slotNum++;
	if (_nextRid.slotNum >= numSlots)
	{
		_nextRid.pageNum++;
		_nextRid.slotNum = 0;
	}
}

bool RBFM_ScanIterator::recordMatchesValue(char* record)
{
	if (_comparasionOp == NO_OP)
	{
		return true;
	}

	// Find the offsets so we can find the attribute value
	unsigned* offsets = (unsigned*)(record + sizeof(unsigned));
	unsigned attributeDataOffset = offsets[_conditionAttributeIndex];
	void* attributeData = record + attributeDataOffset;

	switch(_conditionAttributeType)
	{
	case TypeInt:
		return compareInt(_comparasionOp, record, attributeData);

	case TypeReal:
		return compareReal(_comparasionOp, record, attributeData);

	case TypeVarChar:
		return compareVarChar(_comparasionOp, record, attributeData);
	}

	return false;
}

RC RBFM_ScanIterator::getNextRecord(RID& rid, void* data)
{
	unsigned numPages = _fileHandle->getNumberOfPages();

	// Early exit if our next record is on a non-existant page
	if (_nextRid.pageNum >= numPages)
	{
		return RBFM_EOF;
	}
	
	// Read in the page with the next record
	char pageBuffer[PAGE_SIZE] = {0};
	PageNum loadedPage = _nextRid.pageNum;
	RC ret = _fileHandle->readPage(loadedPage, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Pull in the header preemptively
    PageIndexHeader* pageHeader = RecordBasedFileManager::getPageIndexHeader(pageBuffer);

    // Early exit if our next slot is non-existant
    if (_nextRid.slotNum >= pageHeader->numSlots)
    {
        return RBFM_EOF;
    }

	while(_nextRid.pageNum < numPages)
	{
		// If we are looking on a new page, load that buffer into memory
		if (_nextRid.pageNum != loadedPage)
		{
			loadedPage = _nextRid.pageNum;
			ret = _fileHandle->readPage(loadedPage, pageBuffer);
			if (ret != rc::OK)
			{
				return ret;
			}
		}

		// Attempt to read in the next record
		PageIndexSlot* slot = RecordBasedFileManager::getPageIndexSlot(pageBuffer, _nextRid.slotNum);
		if (slot->size == 0 && slot->nextPage == 0)
		{
			// This record was deleted, skip it
			nextRecord(pageHeader->numSlots);
			continue;
		}

        // Pull up the next record, walking the tombstone chain if necessary
        if (slot->nextPage > 0 || slot->nextSlot > 0) // check to see if we moved to a different page
        {
            unsigned char tempPageBuffer[PAGE_SIZE] = {0};
            while (slot->nextPage > 0 || slot->nextSlot > 0) // walk the forward pointers
            {
                ret = _fileHandle->readPage(slot->nextPage, tempPageBuffer);
                if (ret != rc::OK)
                {
                    return ret;
                }

                slot = RecordBasedFileManager::getPageIndexSlot(tempPageBuffer, slot->nextSlot);
                memcpy(pageBuffer, tempPageBuffer, PAGE_SIZE);
            }
        }

		// Compare record with user's data, skip if it doesn't match
		if (!recordMatchesValue(pageBuffer + slot->pageOffset))
		{
			nextRecord(pageHeader->numSlots);
			continue;
		}

		// Copy over the record to the user's buffer
        unsigned* numAttributes = (unsigned*)(pageBuffer + slot->pageOffset);
		copyRecord((char*)data, pageBuffer + slot->pageOffset, *numAttributes);

		// Advance RID once more and exit
		nextRecord(pageHeader->numSlots);
		return rc::OK;
	}

	return RBFM_EOF;
}

void RBFM_ScanIterator::copyRecord(char* data, const char* record, unsigned numAttributes)
{
	// The offset array is just after the number of attributes
	unsigned* offsets = (unsigned*)((char*)record + sizeof(unsigned));
	unsigned dataOffset = 0;

	// Iterate through all of the columns we actually want to copy for the user
	for (unsigned i=0; i<_returnAttributeIndices.size(); ++i)
	{
		unsigned attributeIndex = _returnAttributeIndices[i];
		unsigned recordOffset = offsets[attributeIndex];
		unsigned attributeSize = Attribute::sizeInBytes(_returnAttributeTypes[i], record + recordOffset);

		// Copy the data and then move forward in the user's buffer
		memcpy(data + dataOffset, record + recordOffset, attributeSize);
		dataOffset += attributeSize;
	}
}

RC RBFM_ScanIterator::close()
{
	if (_comparasionValue)
	{
		free(_comparasionValue);
		_comparasionValue = NULL;
	}

	_fileHandle = NULL;
	_conditionAttributeIndex = -1;
	_returnAttributeIndices.clear();
	_returnAttributeTypes.clear();

	return rc::OK;
}

RC RBFM_ScanIterator::allocateValue(AttrType attributeType, const void* value)
{
	if(_comparasionValue)
	{
		free(_comparasionValue);
	}

	unsigned attributeSize = Attribute::sizeInBytes(attributeType, value);
	if (attributeSize == 0)
	{
		return rc::ATTRIBUTE_INVALID_TYPE;
	}

	_comparasionValue = malloc(attributeSize);
	if (!_comparasionValue)
	{
		return rc::OUT_OF_MEMORY;
	}

	memcpy(_comparasionValue, value, attributeSize);

	return rc::OK;
}

bool RBFM_ScanIterator::compareInt(CompOp op, void* a, void* b)
{
	assert(a != NULL);
	assert(b != NULL);

	switch(op)
	{
	case EQ_OP:		return *(int*)a == *(int*)b;
	case NE_OP:		return *(int*)a != *(int*)b;
	case GE_OP:		return *(int*)a >= *(int*)b;
	case GT_OP:		return *(int*)a >  *(int*)b;
	case LE_OP:		return *(int*)a <= *(int*)b;
	case LT_OP:		return *(int*)a <  *(int*)b;
	case NO_OP:
	default:
		return true;
	}
}

bool RBFM_ScanIterator::compareReal(CompOp op, void* a, void* b)
{
	assert(a != NULL);
	assert(b != NULL);

	switch(op)
	{
	case EQ_OP:		return *(float*)a == *(float*)b;
	case NE_OP:		return *(float*)a != *(float*)b;
	case GE_OP:		return *(float*)a >= *(float*)b;
	case GT_OP:		return *(float*)a >  *(float*)b;
	case LE_OP:		return *(float*)a <= *(float*)b;
	case LT_OP:		return *(float*)a <  *(float*)b;
	case NO_OP:
	default:
		return true;
	}
}

bool RBFM_ScanIterator::compareVarChar(CompOp op, void* a, void* b)
{
	assert(a != NULL);
	assert(b != NULL);

	unsigned lenA = *(unsigned*)a;
	unsigned lenB = *(unsigned*)b;
	char* strA = (char*)a + sizeof(unsigned);
	char* strB = (char*)b + sizeof(unsigned);

	switch(op)
	{
	case EQ_OP:		return (lenA == lenB && (strncmp(strA, strB, lenA) == 0));
	case NE_OP:		return (lenA != lenB || (strncmp(strA, strB, lenA) != 0));
	case GE_OP:		return (                (strncmp(strA, strB, lenA) >= 0));
	case GT_OP:		return (                (strncmp(strA, strB, lenA) >  0));
	case LE_OP:		return (                (strncmp(strA, strB, lenA) <= 0));
	case LT_OP:		return (                (strncmp(strA, strB, lenA) <  0));
	case NO_OP:
	default:
		return true;
	}
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
