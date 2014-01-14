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

class FileHandle;

// PageFile Header
typedef struct
{
  unsigned headerSize;
  unsigned version;
  unsigned numPages;
} PFHeader;

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
    unsigned int free;
    bool loaded;
    void* data;
    int offset;
    int nextEntryAbsoluteOffset; // -1 if end of directory entry list, a real file offset otherwise
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

    RC loadFile(FILE* file, PFHeader* header); 
    RC unload();
    RC flushPages();

    FILE* GetFile() { return _file; }
    PFHeader* GetHeader() { return _header; }
    bool HasFile() const { return _file != NULL; }

private:
    // DB file pointer
    FILE* _file;

    // Collection of page headers - bookkepping
    vector<PFEntry*> _directory; 
    PFHeader* _header;
};

#endif // _pfm_h_
