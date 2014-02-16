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
    : RecordBasedCoreManager(sizeof(RBFM_PageIndexFooter)), _pfm(*PagedFileManager::instance())
{
}

RecordBasedFileManager::~RecordBasedFileManager()
{
    // We don't want our static pointer to be pointing to deleted data in case the object is ever deleted!
	_rbf_manager = NULL;
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

	return insertRecordToPage(fileHandle, recordDescriptor, data, pageNum, rid);
}

RC RecordBasedFileManager::insertRecordToPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, PageNum pageNum, RID &rid) 
{
    // Read in the designated page
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(pageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

    // Recover the index header structure
	RBFM_PageIndexFooter* footer = getRBFMPageIndexFooter(pageBuffer);

	// Verify this page has enough space for the record
	unsigned recLength = 0;
	unsigned recHeaderSize = 0;
	unsigned* recHeader = NULL;
	ret = generateRecordHeader(recordDescriptor, data, recHeader, recLength, recHeaderSize);
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

    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedFileManager::insertRecord: header.freeSpaceOffset = " << footer->freeSpaceOffset << "\n";

    // Write the offsets array and data to disk
    memcpy(pageBuffer + footer->freeSpaceOffset, recHeader, recHeaderSize);
    memcpy(pageBuffer + footer->freeSpaceOffset + recHeaderSize, data, recLength - recHeaderSize);
    free(recHeader);

    // Create a new index slot entry and prepend it to the list
    PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, footer->numSlots);
    slotIndex->size = recLength;
    slotIndex->pageOffset = footer->freeSpaceOffset;
    slotIndex->nextPage = 0; // NULL
    slotIndex->nextSlot = 0; // NULL
    slotIndex->isAnchor = false; // only true if we're the end - anchor - of a tombstone chain

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedFileManager::insertRecord: RID = (" << pageNum << ", " << footer->numSlots << ")\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Writing to: " << PAGE_SIZE - sizeof(RBFM_PageIndexFooter) - ((footer->numSlots + 1) * sizeof(PageIndexSlot)) << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Offset: " << slotIndex->pageOffset << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Size of data: " << recLength << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Header free after record: " << (footer->freeSpaceOffset + recLength) << "\n";

    // Update the header information
    footer->numSlots++;
    footer->freeSpaceOffset += recLength;

    // Update the position of this page in the freespace lists, if necessary
    ret = movePageToCorrectFreeSpaceList(fileHandle, footer);
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
    rid.slotNum = footer->numSlots - 1;

    return rc::OK;
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

    // Pull the target slot into memory
	RBFM_PageIndexFooter* realFooter = getRBFMPageIndexFooter(pageBuffer);
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
		samePageFreeSpace = calculateFreespace(realFooter->freeSpaceOffset, realFooter->numSlots);
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
		realFooter = getRBFMPageIndexFooter(pageBuffer);
        realSlot = getPageIndexSlot(pageBuffer, rid.slotNum);

        // Copy the data to the same place in memory
        assert(realSlot->pageOffset + recLength < (PAGE_SIZE - sizeof(RBFM_PageIndexFooter) - (realFooter->numSlots * sizeof(PageIndexSlot))));
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
        insertRecord(fileHandle, recordDescriptor, data, newRid);

        // Re-read the page into memory, since we can potentially be on the same page
        ret = fileHandle.readPage(rid.pageNum, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Increase the gap size accordingly (the size of the previously stored record)
		realFooter = getRBFMPageIndexFooter(pageBuffer);
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
		realFooter = getRBFMPageIndexFooter(pageBuffer);
        realSlot = getPageIndexSlot(pageBuffer, newRid.slotNum);
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
	PageIndexSlot* slotIndex = getPageIndexSlot(pageBuffer, rid.slotNum);

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
            memcpy(data, &dataLen, sizeof(unsigned));
            memcpy((char*)data + sizeof(unsigned), tempBuffer + offset + sizeof(unsigned), dataLen);
            break;
    }

    // Free up the memory
    free(tempBuffer);

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedFileManager::readAttribute: RID = (" << rid.pageNum << ", " << rid.slotNum << ")\n";;
    dbg::out << "RecordBasedFileManager::readAttribute: Reading from: " << PAGE_SIZE - sizeof(RBFM_PageIndexFooter) - ((rid.slotNum + 1) * sizeof(PageIndexSlot)) << "\n";;
    dbg::out << "RecordBasedFileManager::readAttribute: Offset: " << slotIndex->pageOffset << "\n";
    dbg::out << "RecordBasedFileManager::readAttribute: Size: " << slotIndex->size << "\n";

    return rc::OK;
}

RC RecordBasedFileManager::reorganizeBufferedPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber, unsigned char* pageBuffer)
{
	RC ret = rc::OK;

	// Read in the page header and recover the number of slots
	RBFM_PageIndexFooter* footer = getRBFMPageIndexFooter(pageBuffer);

    // Only proceed if there are actually slots on this page that need to be reordered
    if (footer->numSlots > 0)
    {
        unsigned reallocatedSpace = 0;

        // Read in the record offsets
        PageIndexSlot* offsets = (PageIndexSlot*)malloc(footer->numSlots * sizeof(PageIndexSlot));
        memcpy(offsets, pageBuffer + PAGE_SIZE - sizeof(RBFM_PageIndexFooter) - (footer->numSlots * sizeof(PageIndexSlot)), (footer->numSlots * sizeof(PageIndexSlot)));

        // Find the first non-empty slot
        int offsetIndex = -1;
        for (int i = footer->numSlots - 1; i >= 0; i--)
        {
            if (offsets[i].size != 0 && offsets[i].nextPage == 0) // not deleted, not tombstone
            {
                offsetIndex = i;
                break;
            }
        }

        // Every slot is a tombstone, nothing to reorganize...
        if (offsetIndex == -1)
        {
            return rc::PAGE_CANNOT_BE_ORGANIZED;
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
                    break;
                }
            }

            // Nothing else to be shifted, so hop out of the loop
            if (nextIndex == -1)
            {
                break;
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
        footer->freeSpaceOffset -= footer->gapSize;
        footer->gapSize = 0;
        movePageToCorrectFreeSpaceList(fileHandle, footer);

        // Update the slot entries in the new page
        memcpy(newBuffer + PAGE_SIZE - sizeof(RBFM_PageIndexFooter) - (footer->numSlots * sizeof(PageIndexSlot)), offsets, (footer->numSlots * sizeof(PageIndexSlot)));

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
	std::vector< int > freespace;
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
		RBFM_PageIndexFooter* pageIndexFooter = getRBFMPageIndexFooter(pageBuffer);

		// Keep track of freespace on each page
		freespace.push_back(calculateFreespace(pageIndexFooter->freeSpaceOffset, pageIndexFooter->numSlots));

		// Keep track of the sizes of all records on this page
		recordSizes.push_back(std::vector<unsigned>());
		std::vector<unsigned>& currentPageRecordSizes = recordSizes.back();
		for (unsigned slot=0; slot < pageIndexFooter->numSlots; ++slot)
		{
			PageIndexSlot* pageSlot = getPageIndexSlot(pageBuffer, slot);
			currentPageRecordSizes.push_back(pageSlot->size);
		}
	}

	// Step 2) Don't do bin packing, because it's NP hard
	// Step 3) Find tombstones and collapse them
	// Step 4) Iterate through pages with few records and attempt to move them to consolotate space

	return rc::OK;
}

RC RecordBasedFileManager::scan(FileHandle& fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttributeString, const CompOp compOp, const void *value, const vector<string> &attributeNames, RBFM_ScanIterator &rbfm_ScanIterator)
{
    return rbfm_ScanIterator.init(fileHandle, recordDescriptor, conditionAttributeString, compOp, value, attributeNames);
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

RBFM_PageIndexFooter* RecordBasedFileManager::getRBFMPageIndexFooter(void* pageBuffer)
{
	return (RBFM_PageIndexFooter*)getCorePageIndexFooter(pageBuffer);
}

RC RecordBasedFileManager::freespaceOnPage(FileHandle& fileHandle, PageNum pageNum, int& freespace)
{
	if (pageNum <= 0)
	{
		return rc::PAGE_NUM_INVALID;
	}

	PFHeader header;
	RC ret = readHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (pageNum > header.numPages)
	{
		return rc::PAGE_NUM_INVALID;
	}

	char pageBuffer[PAGE_SIZE] = {0};
	ret = fileHandle.readPage(pageNum, pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

    RBFM_PageIndexFooter *pageIndexFooter = getRBFMPageIndexFooter(pageBuffer);
	freespace = calculateFreespace(pageIndexFooter->freeSpaceOffset, pageIndexFooter->numSlots);

	return rc::OK;
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

RC RBFM_ScanIterator::init(FileHandle& fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttributeString, const CompOp compOp, const void *value, const vector<string> &attributeNames)
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
		Attribute::allocateValue(_conditionAttributeType, value, &_comparasionValue);
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
        return compareInt(_comparasionOp, attributeData, _comparasionValue);

	case TypeReal:
        return compareReal(_comparasionOp, attributeData, _comparasionValue);

	case TypeVarChar:
        return compareVarChar(_comparasionOp, attributeData, _comparasionValue);
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
    RBFM_PageIndexFooter* pageFooter = (RBFM_PageIndexFooter*)RecordBasedCoreManager::getPageIndexFooter(pageBuffer, sizeof(RBFM_PageIndexFooter));

    // Early exit if our next slot is non-existant
    if (_nextRid.slotNum >= pageFooter->numSlots)
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
		PageIndexSlot* slot = RecordBasedCoreManager::getPageIndexSlot(pageBuffer, _nextRid.slotNum, sizeof(RBFM_PageIndexFooter));
		if (slot->size == 0 && slot->nextPage == 0)
		{
			// This record was deleted, skip it
			nextRecord(pageFooter->numSlots);
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

                slot = RecordBasedCoreManager::getPageIndexSlot(tempPageBuffer, slot->nextSlot, sizeof(RBFM_PageIndexFooter));
                memcpy(pageBuffer, tempPageBuffer, PAGE_SIZE);
            }
        }

		// Compare record with user's data, skip if it doesn't match
		if (!recordMatchesValue(pageBuffer + slot->pageOffset))
		{
			nextRecord(pageFooter->numSlots);
			continue;
		}

		// Copy over the record to the user's buffer
        unsigned* numAttributes = (unsigned*)(pageBuffer + slot->pageOffset);
		copyRecord((char*)data, pageBuffer + slot->pageOffset, *numAttributes);

        // Return the RID for the record we just copied out
        rid = _nextRid;

		// Advance RID once more and exit
		nextRecord(pageFooter->numSlots);
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
