#ifndef _pfm_h_
#define _pfm_h_

#include <cstdlib>
#include <cstdio>
#include <vector>

// For convenience
using namespace std;

typedef int RC;
typedef unsigned PageNum;

#define PAGE_SIZE 4096
#define CURRENT_PF_VERSION 1
#define NUM_FREESPACE_LISTS 8

class FileHandle;


// PageFile Header (should fit in 1st page)
struct PFHeader
{
    PFHeader();
    ~PFHeader() {}

    RC validate();

    unsigned headerSize;
    unsigned pageSize;
    unsigned version;
    unsigned numPages;
    unsigned numFreespaceLists;

    unsigned short freespaceCutoffs[NUM_FREESPACE_LISTS];
    PageNum freespaceLists[NUM_FREESPACE_LISTS];
};

class PagedFileManager
{
public:
    static PagedFileManager* instance();                     // Access to the _pf_manager instance

    RC createFile    (const char *fileName);                         // Create a new file
    RC destroyFile   (const char *fileName);                         // Destroy a file
    RC openFile      (const char *fileName, FileHandle &fileHandle); // Open a file
    RC closeFile     (FileHandle &fileHandle);                       // Close a file

protected:
    PagedFileManager();                                   // Constructor
    ~PagedFileManager();                                  // Destructor

private:
    static PagedFileManager *_pf_manager;
};

typedef struct 
{
    PageNum pageNum;
    PageNum nextPage;
    PageNum prevPage;
} PFEntry;

class FileHandle
{
public:
    FileHandle();                                                    // Default constructor
    ~FileHandle();                                                   // Destructor

    RC readPage(PageNum pageNum, void *data);                           // Get a specific page
    RC writePage(PageNum pageNum, const void *data);                    // Write a specific page
    RC appendPage(const void *data);                                    // Append a specific page
    unsigned getNumberOfPages();                                        // Get the number of pages in the file

    RC unload();
    RC flushPages();

    void loadFile(FILE* file, PFHeader* header) { _file = file; _header = header; }
    FILE* getFile() { return _file; }

    PFHeader* getHeader() { return _header; }
    const PFHeader* getHeader() const { return _header; }

    bool hasFile() const { return _file != NULL; }

private:
    // DB file pointer and header information
    FILE* _file;
    PFHeader* _header;
};



#endif // _pfm_h_
