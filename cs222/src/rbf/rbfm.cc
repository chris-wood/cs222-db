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

    // Write out the header data to the reserved page
    ret = _pfm.openFile(fileName.c_str(), handle);
    if (ret != rc::OK)
    {
        return ret;
    }

    header.init();
    ret = header.validate();
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
    PFHeader* header = (PFHeader*)malloc(sizeof(PFHeader));
    size_t read = fread(header, sizeof(PFHeader), 1, fileHandle.getFile());
    if (read != 1)
    {
        return rc::HEADER_SIZE_CORRUPT;
    }

    ret = header->validate();
    if (ret != rc::OK)
    {
        return ret;
    }

    fileHandle.setNumPages(header->numPages);

    _headerData[(unsigned int)fileHandle.getFile()] = header;
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
    PFHeader* header = _headerData[(unsigned int)fileHandle.getFile()];

    // Search through the freespace lists, starting with the smallest size and incrementing upwards
    for (unsigned i=0; i<header->numFreespaceLists; ++i)
    {
        assert(header->freespaceCutoffs[i] > 0);
        if (header->freespaceCutoffs[i] >= bytes)
        {
            if (header->freespaceLists[i] != 0)
            {
                pageNum = header->freespaceLists[i];
                return rc::OK;
            }
        }
    }

    // If we did not find a suitible location, append a new page for the data
    void* page = malloc(PAGE_SIZE);
    memset(page, 0, PAGE_SIZE);
    ret = fileHandle.appendPage(page);
    free(page);
    if (ret != rc::OK)
    {
        return ret;
    }

    header->numPages++;
    writeHeader(fileHandle, header);

    // TODO: Handle the case where bytes > PAGE_SIZE
    pageNum = fileHandle.getNumberOfPages() - 1;

    return rc::OK;
}

RC RecordBasedFileManager::writeHeader(FileHandle &fileHandle, PFHeader* header)
{
    FILE* file = fileHandle.getFile();
    int result = fseek(file, 0, SEEK_SET);
    if (result != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }

    size_t written = fwrite(header, sizeof(*header), 1, file);
    if (written != 1)
    {
        return rc::FILE_CORRUPT;
    }

    return rc::OK;
}

RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) {
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

void PFHeader::init()
{
    headerSize = sizeof(*this);
    pageSize = PAGE_SIZE;
    version = CURRENT_PF_VERSION;
    numPages = 0;
    numFreespaceLists = NUM_FREESPACE_LISTS;

    // Divide the freespace lists evenly, except for the first and last
    unsigned short cutoffDelta = PAGE_SIZE / (NUM_FREESPACE_LISTS + 4);
    unsigned short cutoff = cutoffDelta / 4;

    // Each element starts a linked list of pages, which is garunteed to have at least the cutoff value of bytes free
    // So elements in the largest index will have the most amount of bytes free
    for (int i=0; i<NUM_FREESPACE_LISTS; ++i)
    {
        freespaceLists[i] = 0;
        freespaceCutoffs[i] = cutoff;

        cutoff += cutoffDelta;
    }
}

RC PFHeader::validate()
{
    if (headerSize != sizeof(*this))
        return rc::HEADER_SIZE_CORRUPT;

    if (pageSize != PAGE_SIZE)
        return rc::HEADER_PAGESIZE_MISMATCH;

    if (version != CURRENT_PF_VERSION)
        return rc::HEADER_VERSION_MISMATCH;

    if (numFreespaceLists != NUM_FREESPACE_LISTS)
        return rc::HEADER_FREESPACE_LISTS_MISMATCH;

    return rc::OK;
}
