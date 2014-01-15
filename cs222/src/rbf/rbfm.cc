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
        return ret;
    }

    ret = writeHeader(handle, &header);
    if (ret != rc::OK)
    {
        _pfm.closeFile(handle);
        return ret;
    }

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
        // Note: there are no slots yet, so there are no slotOffsets appended to this struct
        PageIndexHeader index;
        index.freeSpaceOffset = 0;
        index.numSlots = 0;
        index.pageNumber = fileHandle.getNumberOfPages();
        index.prevPage = 0;
        index.nextPage = 0;

        // Append this page to the list of free pages (into the beginning of the largest freespace slot)
        unsigned freespaceSlot = header->numFreespaceLists - 1;
        if (header->freespaceLists[freespaceSlot] == 0)
        {
            header->freespaceLists[freespaceSlot] = index.pageNumber;
        }
        else
        {
            index.nextPage = header->freespaceLists[freespaceSlot];
            header->freespaceLists[index.nextPage] = index.pageNumber;
            header->freespaceLists[freespaceSlot] = index.pageNumber;
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

RC RecordBasedFileManager::writeHeader(FileHandle &fileHandle, PFHeader* header)
{
    unsigned char* buffer = (unsigned char*)malloc(PAGE_SIZE);
    memcpy(buffer, header, sizeof(PFHeader));

    if (fileHandle.getNumberOfPages() == 0)
    {
        // printf("appending header...\n");
        fileHandle.appendPage(buffer);
    }
    else
    {
        // printf("writing existing header...\n"); 
        fileHandle.writePage(0, buffer);
    }

    free(buffer);

    return rc::OK;
}

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) 
{
    // Compute the size of the record to be inserted
    int recLength = recordDescriptor.size();
    for (vector<Attribute>::const_iterator itr = recordDescriptor.begin(); itr != recordDescriptor.end(); itr++)
    {
        recLength += itr->length;
    }

    // Find the first page(s) with enough free space to hold this record
    int requiredPages = 1 + ((recLength - 1) / PAGE_SIZE);
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

    // Copy the record to the freespace offset indicated by the header
    memcpy(pageBuffer + header->freeSpaceOffset, data, recLength);

    // Create a new index slot entry and prepend it to the list
    PageIndexSlot slotIndex;
    slotIndex.size = recLength;
    slotIndex.pageOffset = header->freeSpaceOffset;
    memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - ((header->numSlots + 1) * sizeof(PageIndexSlot)), &slotIndex, sizeof(PageIndexSlot));

    // TODO: need to move this page around in the list structures to see where it actually goes...

    // Update the header information
    header->numSlots++;
    header->freeSpaceOffset += recLength;
    memcpy(pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader), header, sizeof(PageIndexHeader));

    // Write the new page information to disk
    ret = fileHandle.writePage(pageNum, pageBuffer);
    if (ret != rc::OK)
    {
        return ret;
    }

    // Once the write is committed, store the RID information and return
    rid.pageNum = pageNum;
    rid.slotNum = header->numSlots;

    return rc::OK;
}

// NOTE: THIS METHOD MUST RUN IN O(1) TIME
RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) 
{
    // TODO: need to handle the case where n >= 1 pages are used to store the record
    // int requiredPages = ???

    // Pull the page into memory
    unsigned char* pageBuffer = (unsigned char*)malloc(PAGE_SIZE);
    fileHandle.readPage(rid.pageNum, (void*)pageBuffer);

    // Find the slot where the record is stored
    PageIndexSlot* slotIndex = (PageIndexSlot*)malloc(sizeof(PageIndexSlot));
    memcpy(slotIndex, pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader) - (rid.slotNum * sizeof(PageIndexSlot)), sizeof(PageIndexSlot));

    // Copy the contents of the record into the data block
    memcpy(data, pageBuffer + slotIndex->pageOffset, slotIndex->size);

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
    unsigned short cutoffDelta = PAGE_SIZE / (NUM_FREESPACE_LISTS + 4);
    unsigned short cutoff = cutoffDelta / 4;

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
