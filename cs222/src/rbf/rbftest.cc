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
    RC rc;

    cout << "PagedFileManager tests" << endl;
    PagedFileManager *pfm = PagedFileManager::Instance();

    // Clean up old files
    pfm->DestroyFile("testFile0.db");
    pfm->DestroyFile("testFile1.db");
    pfm->DestroyFile("testFile2.db");

    ++numTests;
    if ((rc = pfm->CreateFile("testFile0.db")) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") CreateFile did not succesfully create a testFile0.db (" << rc << ")" << endl;
    }

    ++numTests;
    FileHandle handle0;
    if ((rc = pfm->OpenFile("testFile0.db", handle0)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") OpenFile did not succesfully open testFile0.db (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->CloseFile(handle0)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") CloseFile did not succesfully close handle0 (testFile0.db) (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->DestroyFile("testFile0.db")) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") DestroyFile did not succesfully destroy testFile0.db (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->DestroyFile("testFile1.db")) == 0)
    {
        cout << numTests << ") DestroyFile did not return an error when trying to destroy a non-existant file" << endl;
    }
    else
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }

    ++numTests;
    if ((rc = pfm->CreateFile("testFile1.db")) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") CreateFile did not succesfully create a new file (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->CreateFile("testFile1.db")) == 0)
    {
        cout << numTests << ") CreateFile did not return an error when trying to create a file that already exists" << endl;
    }
    else
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }

    ++numTests;
    FileHandle handle1;
    if ((rc = pfm->CloseFile(handle1)) == 0)
    {
        cout << numTests << ") CloseFile did not return an error when trying to close a file that was never opened" << endl;
    }
    else
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }

    ++numTests;
    FileHandle handle2;
    if ((rc = pfm->OpenFile("testFile2.db", handle2)) == 0)
    {
        cout << numTests << ") OpenFile did not return an error when trying to open a file that does not exist" << endl;
    }
    else
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }

    ++numTests;
    if ((rc = pfm->CloseFile(handle2)) == 0)
    {
        cout << numTests << ") CloseFile did not return an error when trying to close a file that was never opened on a handle that had an illegal OpenFile called on it" << endl;
    }
    else
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }

    ++numTests;
    if ((rc = pfm->OpenFile("testFile1.db", handle1)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") OpenFile did not succesfully open a valid file (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->OpenFile("testFile2.db", handle1)) == 0)
    {
        cout << numTests << ") OpenFile did not return an error when we tried to overwrite an existing file handle" << endl;
    }
    else
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }

    ++numTests;
    FileHandle handle1_copy;
    if ((rc = pfm->OpenFile("testFile1.db", handle1_copy)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") OpenFile did not succesfully open a file (which we already opened) on a different handle (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->CloseFile(handle1_copy)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") CloseFile did not succesfully close a valid file (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->CloseFile(handle1_copy)) == 0)
    {
        cout << numTests << ") CloseFile did not return an error when we tried to close a handle that was already closed" << endl;
    }
    else
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }

    ++numTests;
    if ((rc = pfm->OpenFile("testFile1.db", handle1_copy)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") OpenFile did not succesfully open a file using a recycled handle (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->CloseFile(handle1)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") CloseFile did not succesfully close a valid file (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->CloseFile(handle1_copy)) == 0)
    {
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") CloseFile did not succesfully close a valid file that was recycled (" << rc << ")" << endl;
    }

    ++numTests;
    if ((rc = pfm->DestroyFile("testFile1.db")) == 0)
    {
        // TODO: Are we supposed to be able to destroy a file if there are open handles to it?
        // Right now we will fail to delete a file if a handle is left open
        ++numPassed;
        cout << numTests << ") OK" << endl;
    }
    else
    {
        cout << numTests << ") DestroyFile did not succesfully destroy a file (" << rc << ")" << endl;
    }

    cout << "\nPFM Tests complete: " << numPassed << "/" << numTests << endl;
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
