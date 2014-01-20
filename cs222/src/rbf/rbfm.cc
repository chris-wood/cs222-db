#include "returncodes.h"
#include "rbfm.h"

#include <assert.h>
#include <cstring>
#include <iostream>

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

    // Write out the header data to the reserved page (page 0)
    ret = _pfm.openFile(fileName.c_str(), handle);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Initialize and validate the header for the new file
    init(header);
    ret = validate(header);
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
    ret = validate(header);
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
	unsigned char listSwapBuffer[PAGE_SIZE];

    unsigned requiredPages = 1 + ((bytes - 1) / PAGE_SIZE);
    unsigned char* newPages = (unsigned char*)malloc(requiredPages * PAGE_SIZE);
	if (!newPages)
	{
		return rc::OUT_OF_MEMORY;
	}
	
	// Zero out memory for cleanliness
    memset(newPages, 0, PAGE_SIZE * requiredPages);
	memset(listSwapBuffer, 0, PAGE_SIZE);

    for (unsigned i=0; i<requiredPages; ++i)
    {
        // Create the blank index structure on each page (at the end)
        // Note: there are no slots yet, so there are no slotOffsets prepended to this struct on disk
        PageIndexHeader index;
        index.freeSpaceOffset = 0;
        index.numSlots = 0;
        index.pageNumber = fileHandle.getNumberOfPages();
        index.prevPage = 0;
        index.nextPage = 0;
        index.freespaceList = header.numFreespaceLists - 1; // This is an empty page right now, put it in the largest slot

        // Append this page to the list of free pages (into the beginning of the largest freespace slot)
        FreeSpaceList& oldFreeSpaceList = header.freespaceLists[index.freespaceList];
        if (oldFreeSpaceList.listHead == 0)
        {
            oldFreeSpaceList.listHead = index.pageNumber;
        }
        else
        {
            // Read in the previous list head
            PageIndexHeader previousListHead;
            ret = fileHandle.readPage(oldFreeSpaceList.listHead, listSwapBuffer);
            if (ret != rc::OK)
            {
                free(newPages);
                return ret;
            }

            // Udpate the prev pointer of the previous head to be our page number
            memcpy((void*)&previousListHead, listSwapBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));
            previousListHead.prevPage = pageNum;
            memcpy(listSwapBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&previousListHead, sizeof(PageIndexHeader));

            ret = fileHandle.writePage(previousListHead.pageNumber, listSwapBuffer);

            if (ret != rc::OK)
            {
                free(newPages);
                return ret;
            }

            // Update the header info so the list points to the newly created page as the head
            oldFreeSpaceList.listHead = index.pageNumber;
            index.nextPage = previousListHead.pageNumber;
        }

        memcpy(newPages + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&index, sizeof(PageIndexHeader));

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

    unsigned freespace = PAGE_SIZE - pageHeader.freeSpaceOffset - sizeof(PageIndexHeader) - (pageHeader.numSlots * sizeof(PageIndexSlot));
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
	unsigned char pageBuffer[PAGE_SIZE];

	ret = readHeader(fileHandle, &header);
	if (ret != rc::OK)
	{
		return ret;
	}

    // Update prevPage to point to our nextPage
    if (pageHeader.prevPage > 0)
    {
        // Read in the previous page
        PageIndexHeader prevPageHeader;
        ret = fileHandle.readPage(pageHeader.prevPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Update the next pointer of the previous page to our next pointer
        memcpy((void*)&prevPageHeader, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));
        prevPageHeader.nextPage = pageHeader.nextPage;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&prevPageHeader, sizeof(PageIndexHeader));

        ret = fileHandle.writePage(prevPageHeader.pageNumber, pageBuffer);
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
        PageIndexHeader nextPageHeader;
        ret = fileHandle.readPage(pageHeader.nextPage, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Udpate the prev pointer of the next page to our prev pointer
        memcpy((void*)&nextPageHeader, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));
        nextPageHeader.prevPage = pageHeader.prevPage;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&nextPageHeader, sizeof(PageIndexHeader));

        ret = fileHandle.writePage(nextPageHeader.pageNumber, pageBuffer);
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
        PageIndexHeader listFirstPageHeader;
        ret = fileHandle.readPage(destinationList.listHead, pageBuffer);
        if (ret != rc::OK)
        {
            return ret;
        }

        // Udpate the prev pointer of the previous head to be our page number
        memcpy((void*)&listFirstPageHeader, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));
        listFirstPageHeader.prevPage = pageHeader.pageNumber;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&listFirstPageHeader, sizeof(PageIndexHeader));

        ret = fileHandle.writePage(listFirstPageHeader.pageNumber, pageBuffer);
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
    unsigned char buffer[PAGE_SIZE];
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
    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedFileManager::readHeader(" << fileHandle.getFilename() << ")\n";

    // Allocate the page header buffer and read it in from the disk.
    unsigned char buffer[PAGE_SIZE];
    RC ret = rc::OK;
    if (fileHandle.getNumberOfPages() == 0)
    {
        PFHeader blankHeader;
        init(blankHeader);

        memcpy(buffer, &blankHeader, sizeof(PFHeader));
        ret = fileHandle.appendPage(buffer);
    }

    // Verify correctness
    if(ret == rc::OK)
    {
        ret = fileHandle.readPage(0, buffer);
        memcpy(header, buffer, sizeof(PFHeader));
    }

    return ret;
}

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) 
{
    // Compute the size of the record to be inserted
    unsigned offsetFieldsSize = sizeof(unsigned) * recordDescriptor.size();
    unsigned recLength = sizeof(unsigned) * recordDescriptor.size();
    unsigned offsetIndex = 0;
    unsigned dataOffset = 0;

    // Allocate an array of offets with N entries, where N is the number of fields as indicated
    // by the recordDescriptor vector. Each entry i in this array points to the address offset,
    // from the base address of the record on disk, where the i-th field is stored. 
	unsigned* offsets = (unsigned*)malloc(offsetFieldsSize);
	if (!offsets)
	{
		return rc::OUT_OF_MEMORY;
	}

    // Compute the compact record length and values to be inserted into the offset array
    for (vector<Attribute>::const_iterator itr = recordDescriptor.begin(); itr != recordDescriptor.end(); itr++)
    {
        // First, store the offset
        offsets[offsetIndex++] = recLength;

        // Now bump the length as needed based on the length of the contents
        int count = 0;
        Attribute attr = *itr;
        switch(attr.type)
        {
        case TypeInt:
            recLength += sizeof(int);
            dataOffset += sizeof(int);
            break;

        case TypeReal:
            recLength += sizeof(float);
            dataOffset += sizeof(float);
            break;

        case TypeVarChar:
            memcpy(&count, (char*)data + dataOffset, sizeof(int)); 
            dataOffset += sizeof(int) + count * sizeof(char);
            recLength += sizeof(int) + count * sizeof(char);
            break;

        default:
            free(offsets);
            return rc::ATTRIBUTE_INVALID_TYPE;
        }
    }

    // Find the first page(s) with enough free space to hold this record
    PageNum pageNum;
    RC ret = findFreeSpace(fileHandle, recLength + sizeof(PageIndexSlot), pageNum);
    if (ret != rc::OK)
    {
        free(offsets);
        return ret;
    }

    // Read in the designated page
    unsigned char pageBuffer[PAGE_SIZE];
    ret = fileHandle.readPage(pageNum, pageBuffer);
	if (ret != rc::OK)
	{
		free(offsets);
		return ret;
	}

    // Recover the index header structure
	PageIndexHeader header;
    memcpy(&header, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));

    dbg::out << dbg::LOG_EXTREMEDEBUG << "RecordBasedFileManager::insertRecord: header.freeSpaceOffset = " << header.freeSpaceOffset << "\n";

    // Write the offsets array and data to disk
    memcpy(pageBuffer + header.freeSpaceOffset, offsets, offsetFieldsSize);
    memcpy(pageBuffer + header.freeSpaceOffset + offsetFieldsSize, data, recLength - offsetFieldsSize);

    // Release up the offsets array 
    free(offsets);

    // Create a new index slot entry and prepend it to the list
    PageIndexSlot slotIndex;
    slotIndex.size = recLength;
    slotIndex.pageOffset = header.freeSpaceOffset;
    memcpy(pageBuffer + PAGE_SIZE - (int)sizeof(PageIndexHeader) - (int)((header.numSlots + 1) * sizeof(PageIndexSlot)), &slotIndex, sizeof(PageIndexSlot));

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedFileManager::insertRecord: RID = (" << pageNum << ", " << header.numSlots << ")\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Writing to: " << PAGE_SIZE - (int)sizeof(PageIndexHeader) - (int)((header.numSlots + 1) * sizeof(PageIndexSlot)) << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Offset: " << slotIndex.pageOffset << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Size of data: " << recLength << "\n";
    dbg::out << "RecordBasedFileManager::insertRecord: Header free after record: " << (header.freeSpaceOffset + recLength) << "\n";

    // Update the header information
    header.numSlots++;
    header.freeSpaceOffset += recLength;
    memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), &header, sizeof(PageIndexHeader));

    // Update the position of this page in the freespace lists, if necessary
    ret = movePageToCorrectFreeSpaceList(fileHandle, header);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Copy the updated header information into our page buffer
	memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), &header, sizeof(PageIndexHeader));

    // Write the new page information to disk
    ret = fileHandle.writePage(pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Once the write is committed, store the RID information and return
    rid.pageNum = pageNum;
    rid.slotNum = header.numSlots - 1;

    return rc::OK;
}


RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) 
{    
    // Pull the page into memory - O(1)
    unsigned char pageBuffer[PAGE_SIZE];
    RC ret = fileHandle.readPage(rid.pageNum, (void*)pageBuffer);
	if (ret != rc::OK)
	{
		return ret;
	}

    // Find the slot where the record is stored - O(1)
	PageIndexSlot slotIndex;
    memcpy(&slotIndex, pageBuffer + PAGE_SIZE - (int)sizeof(PageIndexHeader) - (int)((rid.slotNum + 1) * sizeof(PageIndexSlot)), sizeof(PageIndexSlot));

    // Copy the contents of the record into the data block - O(1)
    int fieldOffset = recordDescriptor.size() * sizeof(unsigned);
    memcpy(data, pageBuffer + slotIndex.pageOffset + fieldOffset, slotIndex.size - fieldOffset);

    dbg::out << dbg::LOG_EXTREMEDEBUG;
    dbg::out << "RecordBasedFileManager::readRecord: RID = (" << rid.pageNum << ", " << rid.slotNum << ")\n";;
    dbg::out << "RecordBasedFileManager::readRecord: Reading from: " << PAGE_SIZE - sizeof(PageIndexHeader) - ((rid.slotNum + 1) * sizeof(PageIndexSlot)) << "\n";;
    dbg::out << "RecordBasedFileManager::readRecord: Offset: " << slotIndex.pageOffset << "\n";
    dbg::out << "RecordBasedFileManager::readRecord: Size: " << slotIndex.size << "\n";

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
                int ival = 0;
                memcpy(&ival, (char*)data + offset, sizeof(int));
                offset += sizeof(int);
                out << ival;
                break;
            }
            case TypeReal:
            {
                float rval = 0.0;
                memcpy(&rval, (char*)data + offset, sizeof(float));
                offset += sizeof(float);
                out << rval;
                break;
            }
            case TypeVarChar:
            {
                out << "\"";
                int count = 0;
                memcpy(&count, (char*)data + offset, sizeof(int));
                offset += sizeof(int);
                for (int i=0; i < count; i++)
                {
                    out << ((char*)data)[offset++];
                }
                out << "\"";
            }
        }
        index++;
        if (index != recordDescriptor.size()) out << ",";
    }

    out << ")" << endl;

    return rc::OK;
}

void RecordBasedFileManager::init(PFHeader &header)
{
    header.headerSize = sizeof(PFHeader);;
    header.pageSize = PAGE_SIZE;
    header.version = CURRENT_PF_VERSION;
    header.numPages = 0;
    header.numFreespaceLists = NUM_FREESPACE_LISTS;

    // Divide the freespace lists evenly, except for the first and last
    unsigned short cutoffDelta = PAGE_SIZE / (NUM_FREESPACE_LISTS + 5);
    unsigned short cutoff = cutoffDelta / 8; // Anything under this value is considered a 'full' page

	// the 0th index will have pages that are esentially considered compeltely full
	header.freespaceLists[0].listHead = 0;
    header.freespaceLists[0].cutoff = 0;

    // Each element starts a linked list of pages, which is garunteed to have at least the cutoff value of bytes free
    // So elements in the largest index will have the most amount of bytes free
    for (int i=1; i<NUM_FREESPACE_LISTS; ++i)
    {
        header.freespaceLists[i].listHead = 0;
        header.freespaceLists[i].cutoff = cutoff;
        cutoff += cutoffDelta;
    }
}

RC RecordBasedFileManager::validate(PFHeader &header)
{
    if (header.headerSize != sizeof(PFHeader))
        return rc::HEADER_SIZE_CORRUPT;
 
    if (header.pageSize != PAGE_SIZE)
        return rc::HEADER_PAGESIZE_MISMATCH;
 
    if (header.version != CURRENT_PF_VERSION)
        return rc::HEADER_VERSION_MISMATCH;
 
    if (header.numFreespaceLists != NUM_FREESPACE_LISTS)
        return rc::HEADER_FREESPACE_LISTS_MISMATCH;
 
    return rc::OK;
}
