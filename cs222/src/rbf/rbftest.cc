#include <iostream>
#include <string>
#include <cassert>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <stdio.h>

#include "pfm.h"
#include "rbfm.h"
#include "returncodes.h"

using namespace std;

const int success = 0;


// TODO: Some actual testing framework
#define TEST_FN_PREFIX ++numTests;
#define TEST_FN_POSTFIX(msg) { ++numPassed; cout << ' ' << numTests << ") OK: " << msg << endl; } \
                        else { cout << ' ' << numTests << ") FAIL: " << msg << "<" << rc::rcToString(rc) << ">" << endl; }

#define TEST_FN_EQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) == expected) TEST_FN_POSTFIX(msg)
#define TEST_FN_NEQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) != expected) TEST_FN_POSTFIX(msg)


// Check if a file exists
bool FileExists(string fileName)
{
    struct stat stFileInfo;

    if(stat(fileName.c_str(), &stFileInfo) == 0) return true;
    else return false;
}

void pfmTest()
{
    unsigned numTests = 0;
    unsigned numPassed = 0;
    RC rc;

    cout << "PagedFileManager tests" << endl;
    PagedFileManager *pfm = PagedFileManager::Instance();

    // Clean up old files
    remove("testFile0.db");
    remove("testFile1.db");
    remove("testFile2.db");

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

    cout << "\nPFM Tests complete: " << numPassed << "/" << numTests << "\n\n" << endl;
}


// Function to prepare the data in the correct form to be inserted/read
void prepareTuple(const int nameLength, const string &name, const int age, const float height, const int salary, void *buffer, int *tupleSize)
{
    int offset = 0;

    memcpy((char *)buffer + offset, &nameLength, sizeof(int));
    offset += sizeof(int);
    memcpy((char *)buffer + offset, name.c_str(), nameLength);
    offset += nameLength;

    memcpy((char *)buffer + offset, &age, sizeof(int));
    offset += sizeof(int);

    memcpy((char *)buffer + offset, &height, sizeof(float));
    offset += sizeof(float);

    memcpy((char *)buffer + offset, &salary, sizeof(int));
    offset += sizeof(int);

    *tupleSize = offset;
}

void prepareLargeTuple(const int index, void *buffer, int *size)
{
    int offset = 0;

    // compute the count
    int count = index % 50 + 1;

    // compute the letter
    char text = index % 26 + 97;

    for(int i = 0; i < 10; i++)
    {
        memcpy((char *)buffer + offset, &count, sizeof(int));
        offset += sizeof(int);

        for(int j = 0; j < count; j++)
        {
            memcpy((char *)buffer + offset, &text, 1);
            offset += 1;
        }

        // compute the integer
        memcpy((char *)buffer + offset, &index, sizeof(int));
        offset += sizeof(int);

        // compute the floating number
        float real = (float)(index + 1);
        memcpy((char *)buffer + offset, &real, sizeof(float));
        offset += sizeof(float);
    }
    *size = offset;
}

void createRecordDescriptor(vector<Attribute> &recordDescriptor) {

    Attribute attr;
    attr.name = "EmpName";
    attr.type = TypeVarChar;
    attr.length = (AttrLength)30;
    recordDescriptor.push_back(attr);

    attr.name = "Age";
    attr.type = TypeInt;
    attr.length = (AttrLength)4;
    recordDescriptor.push_back(attr);

    attr.name = "Height";
    attr.type = TypeReal;
    attr.length = (AttrLength)4;
    recordDescriptor.push_back(attr);

    attr.name = "Salary";
    attr.type = TypeInt;
    attr.length = (AttrLength)4;
    recordDescriptor.push_back(attr);

}

void createLargeRecordDescriptor(vector<Attribute> &recordDescriptor)
{
    int index = 0;
    char *suffix = (char *)malloc(10);
    for(int i = 0; i < 10; i++)
    {
        Attribute attr;
        sprintf(suffix, "%d", index);
        attr.name = "attr";
        attr.name += suffix;
        attr.type = TypeVarChar;
        attr.length = (AttrLength)50;
        recordDescriptor.push_back(attr);
        index++;

        sprintf(suffix, "%d", index);
        attr.name = "attr";
        attr.name += suffix;
        attr.type = TypeInt;
        attr.length = (AttrLength)4;
        recordDescriptor.push_back(attr);
        index++;

        sprintf(suffix, "%d", index);
        attr.name = "attr";
        attr.name += suffix;
        attr.type = TypeReal;
        attr.length = (AttrLength)4;
        recordDescriptor.push_back(attr);
        index++;
    }
    free(suffix);
}

int RBFTest_1(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. CreateFile
    cout << "****In RBF Test Case 1****" << endl;

    RC rc;
    string fileName = "test";

    // Create a file named "test"
    rc = pfm->CreateFile(fileName.c_str());
    assert(rc == success);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case 1 Failed!" << endl << endl;
        return -1;
    }

    // Create "test" again, should fail
    rc = pfm->CreateFile(fileName.c_str());
    assert(rc != success);

    cout << "Test Case 1 Passed!" << endl << endl;
    return 0;
}


int RBFTest_2(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. DestroyFile
    cout << "****In RBF Test Case 2****" << endl;

    RC rc;
    string fileName = "test";

    rc = pfm->DestroyFile(fileName.c_str());
    assert(rc == success);

    if(!FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been destroyed." << endl << endl;
        cout << "Test Case 2 Passed!" << endl << endl;
        return 0;
    }
    else
    {
        cout << "Failed to destroy file!" << endl;
        cout << "Test Case 2 Failed!" << endl << endl;
        return -1;
    }
}


int RBFTest_3(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. CreateFile
    // 2. OpenFile
    // 3. GetNumberOfPages
    // 4. CloseFile
    cout << "****In RBF Test Case 3****" << endl;

    RC rc;
    string fileName = "test_1";

    // Create a file named "test_1"
    rc = pfm->CreateFile(fileName.c_str());
    assert(rc == success);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case 3 Failed!" << endl << endl;
        return -1;
    }

    // Open the file "test_1"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Get the number of pages in the test file
    unsigned count = fileHandle.GetNumberOfPages();
    assert(count == (unsigned)0);

    // Close the file "test_1"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    cout << "Test Case 3 Passed!" << endl << endl;

    return 0;
}



int RBFTest_4(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. OpenFile
    // 2. AppendPage
    // 3. GetNumberOfPages
    // 3. CloseFile
    cout << "****In RBF Test Case 4****" << endl;

    RC rc;
    string fileName = "test_1";

    // Open the file "test_1"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Append the first page
    void *data = malloc(PAGE_SIZE);
    for(unsigned i = 0; i < PAGE_SIZE; i++)
    {
        *((char *)data+i) = i % 94 + 32;
    }
    rc = fileHandle.AppendPage(data);
    assert(rc == success);

    // Get the number of pages
    unsigned count = fileHandle.GetNumberOfPages();
    assert(count == (unsigned)1);

    // Close the file "test_1"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    free(data);

    cout << "Test Case 4 Passed!" << endl << endl;

    return 0;
}


int RBFTest_5(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. OpenFile
    // 2. ReadPage
    // 3. CloseFile
    cout << "****In RBF Test Case 5****" << endl;

    RC rc;
    string fileName = "test_1";

    // Open the file "test_1"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Read the first page
    void *buffer = malloc(PAGE_SIZE);
    rc = fileHandle.ReadPage(0, buffer);
    assert(rc == success);

    // Check the integrity of the page
    void *data = malloc(PAGE_SIZE);
    for(unsigned i = 0; i < PAGE_SIZE; i++)
    {
        *((char *)data+i) = i % 94 + 32;
    }
    rc = memcmp(data, buffer, PAGE_SIZE);
    assert(rc == success);

    // Close the file "test_1"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    free(data);
    free(buffer);

    cout << "Test Case 5 Passed!" << endl << endl;

    return 0;
}


int RBFTest_6(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. OpenFile
    // 2. WritePage
    // 3. ReadPage
    // 4. CloseFile
    // 5. DestroyFile
    cout << "****In RBF Test Case 6****" << endl;

    RC rc;
    string fileName = "test_1";

    // Open the file "test_1"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Update the first page
    void *data = malloc(PAGE_SIZE);
    for(unsigned i = 0; i < PAGE_SIZE; i++)
    {
        *((char *)data+i) = i % 10 + 32;
    }
    rc = fileHandle.WritePage(0, data);
    assert(rc == success);

    // Read the page
    void *buffer = malloc(PAGE_SIZE);
    rc = fileHandle.ReadPage(0, buffer);
    assert(rc == success);

    // Check the integrity
    rc = memcmp(data, buffer, PAGE_SIZE);
    assert(rc == success);

    // Close the file "test_1"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    free(data);
    free(buffer);

    // DestroyFile
    rc = pfm->DestroyFile(fileName.c_str());
    assert(rc == success);

    if(!FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been destroyed." << endl;
        cout << "Test Case 6 Passed!" << endl << endl;
        return 0;
    }
    else
    {
        cout << "Failed to destroy file!" << endl;
        cout << "Test Case 6 Failed!" << endl << endl;
        return -1;
    }
}


int RBFTest_7(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. CreateFile
    // 2. OpenFile
    // 3. AppendPage
    // 4. GetNumberOfPages
    // 5. ReadPage
    // 6. WritePage
    // 7. CloseFile
    // 8. DestroyFile
    cout << "****In RBF Test Case 7****" << endl;

    RC rc;
    string fileName = "test_2";

    // Create the file named "test_2"
    rc = pfm->CreateFile(fileName.c_str());
    assert(rc == success);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case 7 Failed!" << endl << endl;
        return -1;
    }

    // Open the file "test_2"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Append 50 pages
    void *data = malloc(PAGE_SIZE);
    for(unsigned j = 0; j < 50; j++)
    {
        for(unsigned i = 0; i < PAGE_SIZE; i++)
        {
            *((char *)data+i) = i % (j+1) + 32;
        }
        rc = fileHandle.AppendPage(data);
        assert(rc == success);
    }
    cout << "50 Pages have been successfully appended!" << endl;

    // Get the number of pages
    unsigned count = fileHandle.GetNumberOfPages();
    assert(count == (unsigned)50);

    // Read the 25th page and check integrity
    void *buffer = malloc(PAGE_SIZE);
    rc = fileHandle.ReadPage(24, buffer);
    assert(rc == success);

    for(unsigned i = 0; i < PAGE_SIZE; i++)
    {
        *((char *)data + i) = i % 25 + 32;
    }
    rc = memcmp(buffer, data, PAGE_SIZE);
    assert(rc == success);
    cout << "The data in 25th page is correct!" << endl;

    // Update the 25th page
    for(unsigned i = 0; i < PAGE_SIZE; i++)
    {
        *((char *)data+i) = i % 60 + 32;
    }
    rc = fileHandle.WritePage(24, data);
    assert(rc == success);

    // Read the 25th page and check integrity
    rc = fileHandle.ReadPage(24, buffer);
    assert(rc == success);

    rc = memcmp(buffer, data, PAGE_SIZE);
    assert(rc == success);

    // Close the file "test_2"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    // DestroyFile
    rc = pfm->DestroyFile(fileName.c_str());
    assert(rc == success);

    free(data);
    free(buffer);

    if(!FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been destroyed." << endl;
        cout << "Test Case 7 Passed!" << endl << endl;
        return 0;
    }
    else
    {
        cout << "Failed to destroy file!" << endl;
        cout << "Test Case 7 Failed!" << endl << endl;
        return -1;
    }
}

int RBFTest_8(PagedFileManager *pfm, RecordBasedFileManager *rbfm) {
    // Functions tested
    // 1. Create File
    // 2. Open File
    // 3. Insert Tuple
    // 4. Read Tuple
    // 5. Close File
    // 6. Destroy File
    cout << "****In RBF Test Case 8****" << endl;

    RC rc;
    string fileName = "test_3";

    // Create a file named "test_3"
    rc = pfm->CreateFile(fileName.c_str());
    assert(rc == success);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case 8 Failed!" << endl << endl;
        return -1;
    }

    // Open the file "test_3"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);


    RID rid;
    int tupleSize = 0;
    void *tuple = malloc(100);
    void *returnedData = malloc(100);

    vector<Attribute> recordDescriptor;
    createRecordDescriptor(recordDescriptor);

    // Insert a tuple into a file
    prepareTuple(6, "Peters", 24, 170.1, 5000, tuple, &tupleSize);
    cout << "Insert Data:" << endl;
    rbfm->printTuple(recordDescriptor, tuple);

    rc = rbfm->insertTuple(fileName, recordDescriptor, tuple, rid);
    assert(rc == success);

    // Given the rid, read the tuple from file
    rc = rbfm->readTuple(fileName, recordDescriptor, rid, returnedData);
    assert(rc == success);

    cout << "Returned Data:" << endl;
    rbfm->printTuple(recordDescriptor, returnedData);

    // Compare whether the two memory blocks are the same
    if(memcmp(tuple, returnedData, tupleSize) != 0)
    {
       cout << "Test Case 8 Failed!" << endl << endl;
        free(tuple);
        free(returnedData);
        return -1;
    }

    // Close the file "test_3"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    // DestroyFile
    rc = pfm->DestroyFile(fileName.c_str());
    assert(rc == success);

    free(tuple);
    free(returnedData);
    cout << "Test Case 8 Passed!" << endl << endl;

    return 0;
}

int RBFTest_9(PagedFileManager *pfm, RecordBasedFileManager *rbfm, vector<RID> &rids, vector<int> &sizes) {
    // Functions tested
    // 1. Create File
    // 2. Open File
    // 3. Insert Multiple Tuples
    // 4. Close File
    cout << "****In RBF Test Case 9****" << endl;

    RC rc;
    string fileName = "test_4";

    // Create a file named "test_4"
    rc = pfm->CreateFile(fileName.c_str());
    assert(rc == success);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case 9 Failed!" << endl << endl;
        return -1;
    }

    // Open the file "test_4"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);


    RID rid;
    void *tuple = malloc(1000);
    int numRecords = 2000;

    vector<Attribute> recordDescriptor;
    createLargeRecordDescriptor(recordDescriptor);

    for(unsigned i = 0; i < recordDescriptor.size(); i++)
    {
        cout << "Attribute Name: " << recordDescriptor[i].name << endl;
        cout << "Attribute Type: " << (AttrType)recordDescriptor[i].type << endl;
        cout << "Attribute Length: " << recordDescriptor[i].length << endl << endl;
    }

    // Insert 2000 tuples into file
    for(int i = 0; i < numRecords; i++)
    {
        // Test insert Tuple
        int size = 0;
        memset(tuple, 0, 1000);
        prepareLargeTuple(i, tuple, &size);

        rc = rbfm->insertTuple(fileName, recordDescriptor, tuple, rid);
        assert(rc == success);

        rids.push_back(rid);
        sizes.push_back(size);
    }
    // Close the file "test_4"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    free(tuple);
    cout << "Test Case 8 Passed!" << endl << endl;

    return 0;
}

int RBFTest_10(PagedFileManager *pfm, RecordBasedFileManager *rbfm, vector<RID> &rids, vector<int> &sizes) {
    // Functions tested
    // 1. Open File
    // 2. Read Multiple Tuples
    // 3. Close File
    // 4. Destroy File
    cout << "****In RBF Test Case 10****" << endl;

    RC rc;
    string fileName = "test_4";

    // Open the file "test_4"
    FileHandle fileHandle;
    rc = pfm->OpenFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    int numRecords = 2000;
    void *tuple = malloc(1000);
    void *returnedData = malloc(1000);

    vector<Attribute> recordDescriptor;
    createLargeRecordDescriptor(recordDescriptor);

    for(int i = 0; i < numRecords; i++)
    {
        memset(tuple, 0, 1000);
        memset(returnedData, 0, 1000);
        rc = rbfm->readTuple(fileName, recordDescriptor, rids[i], returnedData);
        assert(rc == success);

        cout << "Returned Data:" << endl;
        rbfm->printTuple(recordDescriptor, returnedData);

        int size = 0;
        prepareLargeTuple(i, tuple, &size);
        if(memcmp(returnedData, tuple, sizes[i]) != 0)
        {
            cout << "Test Case 10 Failed!" << endl << endl;
            free(tuple);
            free(returnedData);
            return -1;
        }
    }

    // Close the file "test_4"
    rc = pfm->CloseFile(fileHandle);
    assert(rc == success);

    rc = pfm->DestroyFile(fileName.c_str());
    assert(rc == success);

    if(!FileExists(fileName.c_str())) {
        cout << "File " << fileName << " has been destroyed." << endl << endl;
        free(tuple);
        free(returnedData);
        cout << "Test Case 10 Passed!" << endl << endl;
        return 0;
    }
    else {
        cout << "Failed to destroy file!" << endl;
        cout << "Test Case 10 Failed!" << endl << endl;
        free(tuple);
        free(returnedData);
        return -1;
    }
}

int main()
{
    PagedFileManager *pfm = PagedFileManager::Instance();
    RecordBasedFileManager *rbfm = RecordBasedFileManager::Instance();

    pfmTest();

    remove("test");
    remove("test_1");
    remove("test_2");
    remove("test_3");
    remove("test_4");

    vector<RID> rids;
    vector<int> sizes;

    RBFTest_1(pfm);
    RBFTest_2(pfm);
    RBFTest_3(pfm);
    RBFTest_4(pfm);
    RBFTest_5(pfm);
    RBFTest_6(pfm);
    RBFTest_7(pfm);
    RBFTest_8(pfm, rbfm);
    RBFTest_9(pfm, rbfm, rids, sizes);
    RBFTest_10(pfm, rbfm, rids, sizes);

    return 0;
}
