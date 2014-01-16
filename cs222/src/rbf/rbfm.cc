#include "returncodes.h"
#include "rbfm.h"

#include <assert.h>
#include <cstring>

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
}

RC RecordBasedFileManager::createFile(const string &fileName) {
    RC ret = _pfm.createFile(fileName.c_str());
    if (ret != rc::OK)
    {
        printf("wut?\n");
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

    init(header);
    ret = validate(header);
    if (ret != rc::OK)
    {
        printf("validate?\n");
        return ret;
    }

    ret = writeHeader(handle, &header);
    if (ret != rc::OK)
    {
        printf("write?\n");
        _pfm.closeFile(handle);
        return ret;
    }

    ret = _pfm.closeFile(handle);
    if (ret != rc::OK)
    {
        printf("close?\n");
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
    void* headerBuffer = malloc(PAGE_SIZE);
    ret = fileHandle.readPage(0, headerBuffer);
    if (ret != rc::OK)
    {
        return rc::HEADER_SIZE_CORRUPT;
    }

    // Cast to header and then validate it
    PFHeader* header = (PFHeader*)malloc(sizeof(PFHeader));
    memcpy(header, headerBuffer, sizeof(PFHeader));
    ret = validate(*header);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Map the file handle to its header for maintenance
    _headerData[&fileHandle] = header;

    return rc::OK;
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
    PFHeader* header = _headerData[&fileHandle];

    if (bytes < 1)
    {
        return rc::RECORD_SIZE_INVALID;
    }

    // Search through the freespace lists, starting with the smallest size and incrementing upwards
    for (unsigned i=0; i<header->numFreespaceLists; ++i)
    {
        if (header->freespaceCutoffs[i] >= bytes)
        {
            if (header->freespaceLists[i] != 0)
            {
                pageNum = header->freespaceLists[i];
                return rc::OK;
            }
        }
    }

    // If we did not find a suitible location, append however many pages we need to store the data
    unsigned requiredPages = 1 + ((bytes - 1) / PAGE_SIZE);

    // @Tamir: are you okay with this? i.e., specifying the data type and not just void. it's needed for pointer arithetic below
    // @Chris: Yup, makes sense to me
    unsigned char* page = (unsigned char*)malloc(requiredPages * PAGE_SIZE);
    memset(page, 0, PAGE_SIZE * requiredPages);

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
        index.freespaceList = header->numFreespaceLists - 1;

        // Append this page to the list of free pages (into the beginning of the largest freespace slot)
        if (header->freespaceLists[index.freespaceList] == 0)
        {
            header->freespaceLists[index.freespaceList] = index.pageNumber;
        }
        else
        {
            index.nextPage = header->freespaceLists[index.freespaceList];
            header->freespaceLists[index.nextPage] = index.pageNumber;
            header->freespaceLists[index.freespaceList] = index.pageNumber;
        }

        memcpy(page + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&index, sizeof(PageIndexHeader));

        // Append this new page to the file
        ret = fileHandle.appendPage(page);
        free(page);
        if (ret != rc::OK)
        {
            return ret;
        }
    }

    // Store the page number base where this record is stored
    pageNum = fileHandle.getNumberOfPages() - requiredPages; 

    // Update the file header information immediately
    header->numPages = header->numPages + requiredPages;
    ret = writeHeader(fileHandle, header);
    if (ret != rc::OK)
    {
        return ret;
    }

    return rc::OK;
}

RC RecordBasedFileManager::movePageToCorrectFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader)
{
    PFHeader* header = _headerData[&fileHandle];

    unsigned freespace = PAGE_SIZE - pageHeader.freeSpaceOffset - sizeof(PageIndexHeader) - (pageHeader.numSlots * sizeof(PageIndexSlot));
    for (unsigned i=header->numFreespaceLists; i>0; --i)
    {
        unsigned freespaceList = i - 1;
        if (freespace >= header->freespaceCutoffs[freespaceList])
        {
            return movePageToFreeSpaceList(fileHandle, pageHeader, freespaceList);
        }
    }

    return rc::UNKNOWN_FAILURE;
}

RC RecordBasedFileManager::movePageToFreeSpaceList(FileHandle& fileHandle, PageIndexHeader& pageHeader, unsigned destinationListIndex)
{
    if (destinationListIndex == pageHeader.freespaceList)
    {
        // Nothing to do, return
        printf("phew, we're safe\n");
        return rc::OK;
    }

    printf("shuffling: %d %d\n", destinationListIndex, pageHeader.freespaceList);

    RC ret;
    PFHeader* header = _headerData[&fileHandle];
    unsigned char* pageBuffer = (unsigned char*)malloc(PAGE_SIZE);
    memset(pageBuffer, 0, PAGE_SIZE);

    // Update prevPage to point to our nextPage
    if (pageHeader.prevPage > 0)
    {
        printf("updating previous page\n");
        // Read in the previous page
        PageIndexHeader prevPageHeader;
        ret = fileHandle.readPage(pageHeader.prevPage, pageBuffer);
        if (ret != rc::OK)
        {
            free(pageBuffer);
            return ret;
        }

        // Update the next pointer of the previous page to our next pointer
        memcpy((void*)&prevPageHeader, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));
        prevPageHeader.nextPage = pageHeader.nextPage;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&prevPageHeader, sizeof(PageIndexHeader));

        ret = fileHandle.writePage(prevPageHeader.pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            free(pageBuffer);
            return ret;
        }
    }
    else
    {
        printf("updating head of the list\n");
        // We were the beginning of the list, update the head pointer in the file header
        header->freespaceLists[pageHeader.freespaceList] = pageHeader.nextPage;
    }

    // Update nextPage to point to our prevPage
    if (pageHeader.nextPage > 0)
    {
        printf("updating the next page\n");
        // Read in the next page
        PageIndexHeader nextPageHeader;
        ret = fileHandle.readPage(pageHeader.nextPage, pageBuffer);
        if (ret != rc::OK)
        {
            free(pageBuffer);
            return ret;
        }

        // Udpate the prev pointer of the next page to our prev pointer
        memcpy((void*)&nextPageHeader, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));
        nextPageHeader.prevPage = pageHeader.prevPage;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&nextPageHeader, sizeof(PageIndexHeader));

        ret = fileHandle.writePage(nextPageHeader.pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            free(pageBuffer);
            return ret;
        }
    }
    // else { we were at the end of the list, we have nothing to do }

    // Update the first page on the destination list to accomodate us at the head
    if (header->freespaceLists[destinationListIndex] > 0)
    {
        // Read in the page
        PageIndexHeader listFirstPageHeader;
        ret = fileHandle.readPage(header->freespaceLists[destinationListIndex], pageBuffer);
        if (ret != rc::OK)
        {
            free(pageBuffer);
            return ret;
        }

        // Udpate the prev pointer of the previous head to be our page number
        memcpy((void*)&listFirstPageHeader, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));
        listFirstPageHeader.prevPage = pageHeader.pageNumber;
        memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&listFirstPageHeader, sizeof(PageIndexHeader));

        ret = fileHandle.writePage(listFirstPageHeader.pageNumber, pageBuffer);
        if (ret != rc::OK)
        {
            free(pageBuffer);
            return ret;
        }
    }

    // The next page for us is whatever was the previous 1st page
    pageHeader.nextPage = header->freespaceLists[destinationListIndex];

    // Update the header's 1st page to our page number
    header->freespaceLists[destinationListIndex] = pageHeader.pageNumber;
    pageHeader.prevPage = 0;

    // Update our freespace index to the new list
    pageHeader.freespaceList = destinationListIndex;

    // Write out the header to disk with new data
    ret = writeHeader(fileHandle, header);
    if (ret != rc::OK)
    {
        free(pageBuffer);
        return ret;
    }

    memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), (void*)&pageHeader, sizeof(PageIndexHeader));
    ret = fileHandle.writePage(pageHeader.pageNumber, pageBuffer);
    if (ret != rc::OK)
    {
        free(pageBuffer);
        return ret;
    }

    free(pageBuffer);
    return rc::OK;
}

RC RecordBasedFileManager::writeHeader(FileHandle &fileHandle, PFHeader* header)
{
    unsigned char* buffer = (unsigned char*)malloc(PAGE_SIZE);
    memcpy(buffer, header, sizeof(PFHeader));

    // Commit the header to disk
    if (fileHandle.getNumberOfPages() == 0)
    {
        fileHandle.appendPage(buffer);
    }
    else
    { 
        fileHandle.writePage(0, buffer);
    }

    free(buffer);

    return rc::OK;
}

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) 
{
    // Compute the size of the record to be inserted
    unsigned offsetFieldsSize = sizeof(unsigned) * recordDescriptor.size();
    unsigned* offsets = (unsigned*)malloc(offsetFieldsSize);
    unsigned recLength = sizeof(unsigned) * recordDescriptor.size();
    unsigned offsetIndex = 0;
    unsigned dataOffset = 0;
    for (vector<Attribute>::const_iterator itr = recordDescriptor.begin(); itr != recordDescriptor.end(); itr++)
    {
        // First, store the offset
        offsets[offsetIndex++] = recLength;

        // Now bump the length as needed based on the type
        // TODO: get rid of magic numbers and replace with standard sizes
        Attribute attr = *itr;
        if (attr.type == TypeInt || attr.type == TypeReal)
        {
            recLength += 4;
            dataOffset += 4;
        }
        else if (attr.type == TypeVarChar)
        {
            int count = 0;
            memcpy(&count, (char*)data + dataOffset, 4); // TODO: int32_t instead?
            dataOffset += 4 + count;
            recLength += 4 + count;
        }
        else 
        {
            return rc::ATTRIBUTE_INVALID_TYPE;
        }
    }

    // Find the first page(s) with enough free space to hold this record
    // int requiredPages = 1 + ((recLength - 1) / PAGE_SIZE);
    PageNum pageNum;
    RC ret = findFreeSpace(fileHandle, recLength, pageNum);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Read in the designated page
    unsigned char* pageBuffer = (unsigned char*)malloc(PAGE_SIZE);
    fileHandle.readPage(pageNum, pageBuffer);

    // Recover the index header structure
    PageIndexHeader* header = (PageIndexHeader*)malloc(sizeof(PageIndexHeader));
    memcpy(header, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), sizeof(PageIndexHeader));

    // Write the offsets array and data to disk
    memcpy(pageBuffer + header->freeSpaceOffset, offsets, offsetFieldsSize);
    memcpy(pageBuffer + header->freeSpaceOffset + offsetFieldsSize, data, recLength - offsetFieldsSize);

    // Create a new index slot entry and prepend it to the list
    PageIndexSlot slotIndex;
    slotIndex.size = recLength;
    slotIndex.pageOffset = header->freeSpaceOffset;
    printf("\n\nrid = %d %d\n", pageNum, header->numSlots);
    printf("writing to: %d\n", PAGE_SIZE - sizeof(PageIndexHeader) - ((header->numSlots + 1) * sizeof(PageIndexSlot)));
    printf("offset: %d\n", slotIndex.pageOffset);
    printf("size of data: %d\n", recLength);
    memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - ((header->numSlots + 1) * sizeof(PageIndexSlot)), &slotIndex, sizeof(PageIndexSlot));

    // Update the header information
    header->numSlots++;
    header->freeSpaceOffset += recLength;
    printf("header free after record: %d\n", header->freeSpaceOffset);
    memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), header, sizeof(PageIndexHeader));

    // Update the position of this page in the freespace lists, if necessary
    movePageToCorrectFreeSpaceList(fileHandle, *header);

    // Write the new page information to disk
    ret = fileHandle.writePage(pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Once the write is committed, store the RID information and return
    printf("(write) rid.pageNum = %d\n", pageNum);
    rid.pageNum = pageNum;
    rid.slotNum = header->numSlots - 1;
    printf("(post)rid = %d %d\n", rid.pageNum, rid.slotNum);

    return rc::OK;
}

// NOTE: THIS METHOD MUST RUN IN O(1) TIME
RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) 
{
    // TODO: need to handle the case where n >= 1 pages are used to store the record
    // @Chris: the prof said we don't have to handle that case (https://piazza.com/class/hp0w5rnebxg6kn?cid=40)
    // @Tamir: THANK GOD

    printf("(read) rid.pageNum = %d\n", rid.pageNum);

    // Pull the page into memory
    unsigned char* pageBuffer = (unsigned char*)malloc(PAGE_SIZE);
    fileHandle.readPage(rid.pageNum, (void*)pageBuffer);

    // Find the slot where the record is stored
    PageIndexSlot* slotIndex = (PageIndexSlot*)malloc(sizeof(PageIndexSlot));
    printf("reading from: %d\n", PAGE_SIZE - sizeof(PageIndexHeader) - ((rid.slotNum + 1) * sizeof(PageIndexSlot)));
    memcpy(slotIndex, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - ((rid.slotNum + 1) * sizeof(PageIndexSlot)), sizeof(PageIndexSlot));
    printf("offset: %d\n", slotIndex->pageOffset);
    printf("size: %d\n", slotIndex->size);

    // Copy the contents of the record into the data block
    int fieldOffset = recordDescriptor.size() * sizeof(unsigned);
    memcpy(data, pageBuffer + slotIndex->pageOffset + fieldOffset, slotIndex->size);

    return rc::OK;
}

RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) 
{
    // unsigned index = 0;
    // unsigned offset = 0;
    // cout << "(";
    // for (vector<Attribute>::const_iterator itr = recordDescriptor.begin(); itr != recordDescriptor.end(); itr++)
    // {
    //     Attribute attr = *itr;
    //     switch (attr.type)
    //     {
    //         case TypeInt:
    //             int ival = 0;
    //             memcpy(&ival, )
    //             break;
    //         case TypeReal:
    //             int rval = 0.0;
    //             break;
    //         case TypeVarChar:
    //             cout << "\"";
    //             for (unsigned i=0; i < attr.length; i++)
    //             {
    //                 cout << data[offset++];
    //             }
    //             cout << "\"";
    //     }

    //     if (index != recordDescriptor.size()) cout << ","
    // }

    return rc::FEATURE_NOT_YET_IMPLEMENTED;
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
    unsigned short cutoff = 0; // This makes the 0th index basically a dumping ground for full pages

    // Each element starts a linked list of pages, which is garunteed to have at least the cutoff value of bytes free
    // So elements in the largest index will have the most amount of bytes free
    for (int i=0; i<NUM_FREESPACE_LISTS; ++i)
    {
        header.freespaceLists[i] = 0;
        header.freespaceCutoffs[i] = cutoff;
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
