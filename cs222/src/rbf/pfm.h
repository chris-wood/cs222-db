#ifndef _pfm_h_
#define _pfm_h_

#include <cstdio>

typedef int RC;
typedef unsigned PageNum;

#define PAGE_SIZE 4096

class FileHandle;


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


class FileHandle
{
public:
    FileHandle();                                                    // Default constructor
    ~FileHandle();                                                   // Destructor

    RC ReadPage(PageNum pageNum, void *data);                           // Get a specific page
    RC WritePage(PageNum pageNum, const void *data);                    // Write a specific page
    RC AppendPage(const void *data);                                    // Append a specific page
    unsigned GetNumberOfPages();                                        // Get the number of pages in the file

    void SetFile(FILE* file) { _file = file; }
    FILE* GetFile() { return _file; }
    bool HasFile() const { return _file != NULL; }

private:
    FILE* _file;
};

#endif // _pfm_h_
