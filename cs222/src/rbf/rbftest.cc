#include <fstream>
#include <iostream>
#include <cassert>

#include "pfm.h"
#include "rbfm.h"
#include "returncodes.h"

using namespace std;

// TODO: Some actual testing framework
#define TEST_FN_PREFIX ++numTests;
#define TEST_FN_POSTFIX(msg) { ++numPassed; cout << ' ' << numTests << ") OK: " << msg << endl; } \
                        else { cout << ' ' << numTests << ") FAIL: " << msg << "<" << rc::rcToString(rc) << ">" << endl; }

#define TEST_FN_EQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) == expected) TEST_FN_POSTFIX(msg)
#define TEST_FN_NEQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) != expected) TEST_FN_POSTFIX(msg)


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

    FileHandle handle0, handle1, handle1_copy, handle2;

    TEST_FN_EQ( 0, pfm->CreateFile("testFile0.db"), "Create testFile0.db");
    TEST_FN_EQ( 0, pfm->OpenFile("testFile0.db", handle0), "Open testFile0.db and store in handle0");
    TEST_FN_EQ( 0, pfm->CloseFile(handle0), "Close handle0 (testFile0.db)");
    TEST_FN_EQ( 0, pfm->DestroyFile("testFile0.db"), "Destroy testFile0.db");
    TEST_FN_NEQ(0, pfm->DestroyFile("testFile1.db"), "Destroy testFile1.db (does not exist, should fail)");
    TEST_FN_EQ( 0, pfm->CreateFile("testFile1.db"), "Create testFile0.db");
    TEST_FN_NEQ(0, pfm->CreateFile("testFile1.db"), "Create testFile1.db (already exists, should fail)");
    TEST_FN_NEQ(0, pfm->CloseFile(handle1), "Close handl1 (uninitialized, should fail)");
    TEST_FN_NEQ(0, pfm->OpenFile("testFile2.db", handle2), "Open testFile2.db and store in handle2 (does not exist, should fail)");
    TEST_FN_NEQ(0, pfm->CloseFile(handle2), "Close handle2 (should never have been initialized, should fail)");
    TEST_FN_EQ( 0, pfm->OpenFile("testFile1.db", handle1), "Open testFile1.db and store in handle1");
    TEST_FN_NEQ(0, pfm->OpenFile("testFile2.db", handle1), "Open testFile2.db and store in handle2 (initialized, should fail)");
    TEST_FN_EQ( 0, pfm->OpenFile("testFile1.db", handle1_copy), "Open testFile1.db and store in handle1_copy");
    TEST_FN_EQ( 0, pfm->CloseFile(handle1_copy), "Close handle1_copy");
    TEST_FN_NEQ(0, pfm->CloseFile(handle1_copy), "Close handle1_copy (should fail, already closed)");
    TEST_FN_EQ( 0, pfm->OpenFile("testFile1.db", handle1_copy), "Open testFile1.db and store in recycled handle1_copy");
    TEST_FN_EQ( 0, pfm->CloseFile(handle1), "Close handle1");
    TEST_FN_EQ( 0, pfm->CloseFile(handle1_copy), "Close handle1_copy");
    TEST_FN_EQ( 0, pfm->DestroyFile("testFile1.db"), "Destroy testFile1.db");
    // TODO: Are we supposed to be able to destroy a file if there are open handles to it?
    // Right now we will fail to delete a file if a handle is left open

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
