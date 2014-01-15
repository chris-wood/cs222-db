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


class FileHandle;

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
    RC loadFile(FILE* file);

    void setNumPages(unsigned pages) { _numPages = pages; }

    FILE* getFile() { return _file; }
    const FILE* getFile() const { return _file; }

    bool hasFile() const { return _file != NULL; }

    bool operator == (const FileHandle& that) const { return this->_file == that._file; }

private:
    // DB file pointer
    FILE* _file;
    unsigned _numPages;
};



#endif // _pfm_h_
