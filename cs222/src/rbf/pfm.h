#ifndef _pfm_h_
#define _pfm_h_

#include <cstdio>
#include <vector>

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
    static PagedFileManager* Instance();                     // Access to the _pf_manager instance

    RC CreateFile    (const char *fileName);                         // Create a new file
    RC DestroyFile   (const char *fileName);                         // Destroy a file
    RC OpenFile      (const char *fileName, FileHandle &fileHandle); // Open a file
    RC CloseFile     (FileHandle &fileHandle);                       // Close a file

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
} PageEntry;

class FileHandle
{
public:
    FileHandle();                                                    // Default constructor
    ~FileHandle();                                                   // Destructor

    RC ReadPage(PageNum pageNum, void *data);                           // Get a specific page
    RC WritePage(PageNum pageNum, const void *data);                    // Write a specific page
    RC AppendPage(const void *data);                                    // Append a specific page
    unsigned GetNumberOfPages();                                        // Get the number of pages in the file

<<<<<<< HEAD
    RC SetFile(FILE* file); 
=======
    void SetNumberOfPages(unsigned pages) { _numPages = pages; }
    void SetFile(FILE* file) { _file = file; }
>>>>>>> 5ced45340d72508c7bd8709cc690699f88a91164
    FILE* GetFile() { return _file; }
    bool HasFile() const { return _file != NULL; }

private:
    FILE* _file;
<<<<<<< HEAD
    vector<PageEntry*> _directory; // always non-empty list, initialized when openfile is called

=======
    unsigned _numPages;
>>>>>>> 5ced45340d72508c7bd8709cc690699f88a91164
};

#endif // _pfm_h_
