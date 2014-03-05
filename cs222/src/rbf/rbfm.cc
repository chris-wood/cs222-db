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

RC RecordBasedFileManager::reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber)
{
    unsigned char pageBuffer[PAGE_SIZE] = {0};
    RC ret = fileHandle.readPage(pageNumber, pageBuffer);
    if (ret != rc::OK)
	{
		return ret;
	}

	return reorganizeBufferedPage(fileHandle, sizeof(RBFM_PageIndexFooter), recordDescriptor, pageNumber, pageBuffer);
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
		reorganizeBufferedPage(fileHandle, sizeof(RBFM_PageIndexFooter), recordDescriptor, page, pageBuffer);
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

	return compareData(_conditionAttributeType, _comparasionOp, attributeData, _comparasionValue);
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

void RBFM_ScanIterator::copyRecord(char* data, const char* record, unsigned /*numAttributes*/)
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

bool RBFM_ScanIterator::compareData(AttrType type, CompOp op, void* a, void* b)
{
	switch (type)
	{
	case TypeInt:
		//std::cout << "Comparing ints: " << *(int*)a << "vs" << *(int*)b << " == " << compareInt(op, a, b) << std::endl;
		return compareInt(op, a, b);

	case TypeReal:
		return compareReal(op, a, b);

	case TypeVarChar:
		return compareVarChar(op, a, b);

	default:
		return false;
	}
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
