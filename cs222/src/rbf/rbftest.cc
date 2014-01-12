#include <fstream>
#include <iostream>
#include <cassert>

#include "pfm.h"
#include "rbfm.h"

using namespace std;

void pfmTest()
{
    unsigned numTests = 0;
    unsigned numPassed = 0;

    cout << "PagedFileManager tests" << endl;
    PagedFileManager *pfm = PagedFileManager::Instance();

    // Clean up old files
    pfm->DestroyFile("testFile1.db");
    pfm->DestroyFile("testFile2.db");

    ++numTests;
    if (pfm->DestroyFile("testFile1.db") == 0)
    {
        cout << "\nDestroyFile did not return an error when trying to destroy a non-existant file" << endl;
    }
    else
    {
        ++numPassed;
        cout << ".";
    }

    ++numTests;
    if (pfm->CreateFile("testFile1.db") == 0)
    {
        ++numPassed;
        cout << ".";
    }
    else
    {
        cout << "\nCreateFile did not succesfully create a new file" << endl;
    }

    ++numTests;
    if (pfm->CreateFile("testFile1.db") == 0)
    {
        cout << "\nCreateFile did not return an error when trying to create a file that already exists" << endl;
    }
    else
    {
        ++numPassed;
        cout << ".";
    }

    ++numTests;
    FileHandle handle1;
    if (pfm->CloseFile(handle1) == 0)
    {
        cout << "\nCloseFile did not return an error when trying to close a file that was never opened" << endl;
    }
    else
    {
        ++numPassed;
        cout << ".";
    }

    ++numTests;
    FileHandle handle2;
    if (pfm->OpenFile("testFile2.db", handle2) == 0)
    {
        cout << "\nOpenFile did not return an error when trying to open a file that does not exist" << endl;
    }
    else
    {
        ++numPassed;
        cout << ".";
    }

    ++numTests;
    if (pfm->OpenFile("testFile1.db", handle1) == 0)
    {
        ++numPassed;
        cout << ".";
    }
    else
    {
        cout << "\nOpenFile did not succesfully open a valid file" << endl;
    }

    ++numTests;
    if (pfm->OpenFile("testFile2.db", handle1) == 0)
    {
        cout << "\nOpenFile did not return an error when we tried to overwrite an existing file handle" << endl;
    }
    else
    {
        ++numPassed;
        cout << ".";
    }

    ++numTests;
    FileHandle handle1_copy;
    if (pfm->OpenFile("testFile1.db", handle1_copy) == 0)
    {
        ++numPassed;
        cout << ".";
    }
    else
    {
        cout << "\nOpenFile did not succesfully open a file (which we already opened) on a different handle" << endl;
    }

    ++numTests;
    if (pfm->CloseFile(handle1_copy) == 0)
    {
        ++numPassed;
        cout << ".";
    }
    else
    {
        cout << "\nCloseFile did not succesfully close a valid file" << endl;
    }

    ++numTests;
    if (pfm->CloseFile(handle1_copy) == 0)
    {
        cout << "\nCloseFile did not return an error when we tried to close a handle that was already closed" << endl;
    }
    else
    {
        ++numPassed;
        cout << ".";
    }

    ++numTests;
    if (pfm->OpenFile("testFile1.db", handle1_copy) == 0)
    {
        ++numPassed;
        cout << ".";
    }
    else
    {
        cout << "\nOpenFile did not succesfully open a file (using a recycled handle)" << endl;
    }

    ++numTests;
    if (pfm->DestroyFile("testFile1.db") == 0)
    {
        ++numPassed;
        cout << ".";
    }
    else
    {
        cout << "\nDestroyFile did not succesfully destroy a file" << endl;
    }
}

void rbfTest()
{
    PagedFileManager *pfm = PagedFileManager::Instance();
    RecordBasedFileManager *rbfm = RecordBasedFileManager::Instance();

    // write your own testing cases here
}


int main() 
{
    cout << "test..." << endl;

    pfmTest();
    rbfTest();
    // other tests go here

    cout << "Done" << endl;
}
