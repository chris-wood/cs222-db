#include "pfm.h"
#include "../util/returncodes.h"

#include <sys/stat.h>
#include <assert.h>
#include <cstring>
#include <cstdlib>

PagedFileManager* PagedFileManager::_pf_manager = 0;


PagedFileManager* PagedFileManager::instance()
{
    if(!_pf_manager)
    {
        _pf_manager = new PagedFileManager();
    }

    return _pf_manager;
}


PagedFileManager::PagedFileManager()
{
}


PagedFileManager::~PagedFileManager()
{
    // We don't want our static pointer to be pointing to deleted data in case the object is ever deleted!
	_pf_manager = NULL;
}


RC PagedFileManager::createFile(const char *fileName)
{
    // Check if the file exists - error if it already exists
    FILE* file = fopen(fileName, "rb");
    if (file)
    {
        fclose(file);
        return rc::FILE_ALREADY_EXISTS;
    }

    // Open for binary write - to create the file
    file = fopen(fileName, "wb");
    if (file)
    {
        fclose(file);
    }

    return rc::OK;
}


RC PagedFileManager::destroyFile(const char *fileName)
{    
    // Refuse to destroy the file if it's open
    string fname = std::string(fileName);
    map<std::string, int>::iterator itr = _openFileCount.find(fname);
    if (itr == _openFileCount.end() || _openFileCount[fname] == 0)
    {
        if (remove(fileName) != 0)
        {
            return rc::FILE_COULD_NOT_DELETE;
        }
        return rc::OK;
    }
    else
    {
        return rc::FILE_COULD_NOT_DELETE;
    }
}


RC PagedFileManager::openFile(const char *fileName, FileHandle &fileHandle)
{
    // Check for existing initialization (filehandle is already a handle for an open file)
    if (fileHandle.hasFile())
    {
        return rc::FILE_HANDLE_ALREADY_INITIALIZED;
    }

    // Open file read only (will not create) to see if it exists
    FILE* file = fopen(fileName, "rb");
    if (!file)
    {
        return rc::FILE_NOT_FOUND;
    }
    else
    {
        // Open file for reading/writing
        fclose(file);
        file = fopen(fileName, "rb+");
        if (!file)
        {
            // Note: we will never hit this case if the above fopen succeeds
            return rc::FILE_COULD_NOT_OPEN;
        }

        // Initialize the FileHandle
        RC ret = fileHandle.loadFile(fileName, file);
        if (ret != rc::OK)
        {
            // Note: we only hit this case if fseek() fails
            return ret;
        }

        // Mark the file as open, or increment the count if already open
        string fname = std::string(fileName);
        map<std::string, int>::iterator itr = _openFileCount.find(fname);
        if (itr == _openFileCount.end())
        {
            _openFileCount[fname] = 1;
        }
        else
        {
            _openFileCount[fname]++;
        }

        return rc::OK;
    }
}


RC PagedFileManager::closeFile(FileHandle &fileHandle)
{
    if (!fileHandle.hasFile())
    {
        return rc::FILE_HANDLE_NOT_INITIALIZED;
    }

    // Check to make sure someone didn't call loadFile() directly
    map<std::string, int>::iterator itr = _openFileCount.find(fileHandle.getFilename());
    if (itr == _openFileCount.end()) // not even an open file - error
    {
        // Note: we will never hit this case 
        return rc::FILE_COULD_NOT_DELETE;
    }

	// Unload before decrementing and returning
    fileHandle.unloadFile();
    _openFileCount[fileHandle.getFilename()]--;

    //// NOTE: we do not explicitly flush to disk since all calls to write/append
    ////       immediately flush data to disk - doing so here would be redundant.

    return rc::OK;
}


FileHandle::FileHandle()
    : _file(NULL)
{
}


FileHandle::~FileHandle()
{
    unloadFile();
}

RC FileHandle::updatePageCount()
{
    if (fseek(_file, 0, SEEK_END) != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }
    _numPages = ftell(_file) / PAGE_SIZE;
    return rc::OK;
}

RC FileHandle::loadFile(const char *fileName, FILE* file)
{
    _filename = std::string(fileName);
    _file = file;
    return updatePageCount();
}

RC FileHandle::unloadFile()
{
    // Prepare handle for reuse
    if (_file)
    {
        fclose(_file);
    }

    _file = NULL;

    return rc::OK;
}

RC FileHandle::readPage(PageNum pageNum, void *data)
{
    RC ret = updatePageCount();
    if (ret != rc::OK)
    {
        // Note: we will only his this case if fseek() fails
        return ret;
    }
    else if (pageNum < _numPages)
    {
        // Read the data from disk into the user buffer
        int result = fseek(_file, PAGE_SIZE * pageNum, SEEK_SET);
        if (result != 0)
        {
            return rc::FILE_SEEK_FAILED;
        }
        size_t read = fread(data, PAGE_SIZE, 1, _file);
        if (read != 1)
        {
            return rc::FILE_CORRUPT;
        }

        return rc::OK;
    }
    else
    {
        return rc::FILE_PAGE_NOT_FOUND;
    }
}

// This shouldn't be here, just putting this here to do quick tests to make it easier to see when the header gets corrupted
struct PageIndexHeader
{
  unsigned pageNumber;
  unsigned freeSpaceOffset;
  unsigned numSlots;
  unsigned gapSize;

  // Doubly linked list data for whichever freespace list we are attached to
  unsigned freespaceList;
  PageNum prevPage;
  PageNum nextPage;
};

const PageIndexHeader* getPageIndexHeader(const void* pageBuffer)
{
	return (const PageIndexHeader*)((char*)pageBuffer + PAGE_SIZE - sizeof(PageIndexHeader));
}

RC FileHandle::writePage(PageNum pageNum, const void *data)
{
	// Super basic checks to see if this write has corrupted data
	// const PageIndexHeader* header = getPageIndexHeader(data);
	// assert(header->freespaceList < 32);
	// assert(header->freeSpaceOffset < PAGE_SIZE);
	// assert(header->gapSize < PAGE_SIZE);
	// assert(header->nextPage <= _numPages);
	// assert(header->prevPage <= _numPages);
	// assert(header->pageNumber <= _numPages);

    RC ret = updatePageCount();
    if (ret != rc::OK)
    {
        // Note: we will only his this case if fseek() fails
        return ret;
    }
    else if (pageNum < _numPages)
    {
        // Flush the content in the user buffer to disk and update the page entry
        int result = fseek(_file, PAGE_SIZE * pageNum, SEEK_SET);
        if (result != 0)
        {
            return rc::FILE_SEEK_FAILED;
        }
        size_t written = fwrite(data, PAGE_SIZE, 1, _file);
        if (written != 1)
        {
            return rc::FILE_CORRUPT;
        }

        return rc::OK;
    }
    else
    {
        return rc::FILE_PAGE_NOT_FOUND;
    }
}


RC FileHandle::appendPage(const void *data)
{
    // Seek to the end of the file (last page) and write the new page data
    if (fseek(_file, _numPages * PAGE_SIZE, SEEK_SET) != 0)
    {
        return rc::FILE_SEEK_FAILED;
    }
    size_t written = fwrite(data, PAGE_SIZE, 1, _file);
    if (written != 1)
    {
        return rc::FILE_CORRUPT;
    }

    // Update our copy of the page count, after committing to disk
    return updatePageCount();
}


unsigned FileHandle::getNumberOfPages()
{
    // Force update in case someone else modified the file through a different handle
    updatePageCount();
    return _numPages;
}

