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

RC RecordBasedFileManager::readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data)
{
    // Pull the page into memory - O(1)
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(rid.pageNum, pageBuffer);
    RETURN_ON_ERR(ret);

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
	RBFM_PageIndexFooter* oldFooter = getRBFMPageIndexFooter(pageBuffer);

    // Only proceed if there are actually slots on this page that need to be reordered
    if (oldFooter->numSlots > 0)
    {
        unsigned reallocatedSpace = 0;

        // Read in the record offsets
        PageIndexSlot* offsets = (PageIndexSlot*)malloc(oldFooter->numSlots * sizeof(PageIndexSlot));
        memcpy(offsets, pageBuffer + PAGE_SIZE - sizeof(RBFM_PageIndexFooter) - (oldFooter->numSlots * sizeof(PageIndexSlot)), (oldFooter->numSlots * sizeof(PageIndexSlot)));

        // Find the first non-empty slot
        int offsetIndex = -1;
        for (int i = oldFooter->numSlots - 1; i >= 0; i--)
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
		RBFM_PageIndexFooter* newFooter = getRBFMPageIndexFooter(newBuffer);

		// Copy the old footer to the new buffer
		memcpy(newFooter, oldFooter, sizeof(RBFM_PageIndexFooter));

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
        newFooter->freeSpaceOffset -= newFooter->gapSize;
        newFooter->gapSize = 0;
        movePageToCorrectFreeSpaceList(fileHandle, newFooter);

        // Update the slot entries in the new page
        memcpy(newBuffer + PAGE_SIZE - sizeof(RBFM_PageIndexFooter) - (newFooter->numSlots * sizeof(PageIndexSlot)), offsets, (newFooter->numSlots * sizeof(PageIndexSlot)));

        // Push the changes to disk
        ret = fileHandle.writePage(pageNumber, (void*)newBuffer);
        RETURN_ON_ERR(ret);
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
    RETURN_ON_ERR(ret);

	return reorganizeBufferedPage(fileHandle, recordDescriptor, pageNumber, pageBuffer);
}

RC RecordBasedFileManager::reorganizeFile(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor)
{
	PFHeader header;
	RC ret = readHeader(fileHandle, &header);
	RETURN_ON_ERR(ret);

	unsigned char pageBuffer[PAGE_SIZE] = {0};
	std::vector< int > freespace;
	std::vector< std::vector<unsigned> > recordSizes;

	// Step 1) Reorganize individual pages
	for (unsigned page=0; page<header.numPages; ++page)
	{
		fileHandle.readPage(page, pageBuffer);
		RETURN_ON_ERR(ret);

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
	RETURN_ON_ERR(ret);

	if (pageNum > header.numPages)
	{
		return rc::PAGE_NUM_INVALID;
	}

	char pageBuffer[PAGE_SIZE] = {0};
	ret = fileHandle.readPage(pageNum, pageBuffer);
	RETURN_ON_ERR(ret);

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
		RETURN_ON_ERR(ret);
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
		RETURN_ON_ERR(ret);

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
	RETURN_ON_ERR(ret);

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
			RETURN_ON_ERR(ret);
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
				RETURN_ON_ERR(ret);

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
