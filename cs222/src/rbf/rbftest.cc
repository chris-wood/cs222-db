#include <iostream>
#include <string>
#include <cassert>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <stdio.h>
#include <vector>

#include "pfm.h"
#include "rbfm.h"
#include "returncodes.h"

using namespace std;

const int success = 0;

// Check if a file exists
bool FileExists(string fileName)
{
    struct stat stFileInfo;

    if(stat(fileName.c_str(), &stFileInfo) == 0) return true;
    else return false;
}

// TODO: Some actual testing framework
#define TEST_FN_PREFIX ++numTests;
#define TEST_FN_POSTFIX(msg) { ++numPassed; cout << ' ' << numTests << ") OK: " << msg << endl; } \
                        else { cout << ' ' << numTests << ") FAIL: " << msg << "<" << rc::rcToString(rc) << ">" << endl; }

#define TEST_FN_EQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) == expected) TEST_FN_POSTFIX(msg)
#define TEST_FN_NEQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) != expected) TEST_FN_POSTFIX(msg)

bool testSmallRecords1(FileHandle& fileHandle)
{
	int numRecords = 10000;
	RecordBasedFileManager *rbfm = RecordBasedFileManager::instance();

	Attribute nanoRecordDescriptor;
	nanoRecordDescriptor.name = "Nano";
    nanoRecordDescriptor.type = TypeInt;
    nanoRecordDescriptor.length = (AttrLength)4;

	Attribute tinyRecordDescriptor;
	tinyRecordDescriptor.name = "Tiny";
	tinyRecordDescriptor.type = TypeVarChar;
    tinyRecordDescriptor.length = (AttrLength)1;

	Attribute smallRecordDescriptor;
	smallRecordDescriptor.name = "Small";
	smallRecordDescriptor.type = TypeVarChar;
    smallRecordDescriptor.length = (AttrLength)3;

	std::vector<RID> nanoRids;
	std::vector<char*> nanoRecords;
	std::vector<char*> nanoReturnedRecords;
	std::vector<Attribute> nanoRecordAttributes;

	std::vector<RID> tinyRids;
	std::vector<char*> tinyRecords;
	std::vector<char*> tinyReturnedRecords;
	std::vector<Attribute> tinyRecordAttributes;

	std::vector<RID> smallRids;
	std::vector<char*> smallRecords;
	std::vector<char*> smallReturnedRecords;
	std::vector<Attribute> smallRecordAttributes;

	size_t nanoSize = nanoRecordDescriptor.length;
	size_t tinySize = tinyRecordDescriptor.length + sizeof(int);
	size_t smallSize = smallRecordDescriptor.length + sizeof(int);

	// Allocate memory for the data and for the return data
	for( int i=0; i<numRecords; ++i )
	{
		nanoRids.push_back(RID());
		nanoRecords.push_back( (char*)malloc(nanoSize) );
		nanoReturnedRecords.push_back( (char*)malloc(nanoSize) );

		tinyRids.push_back(RID());
		tinyRecords.push_back ( (char*)malloc(tinySize) );
		tinyReturnedRecords.push_back ( (char*)malloc(tinySize) );

		smallRids.push_back(RID());
		smallRecords.push_back ( (char*)malloc(smallSize) );
		smallReturnedRecords.push_back ( (char*)malloc(smallSize) );
	}

	nanoRecordAttributes.push_back(nanoRecordDescriptor);
	tinyRecordAttributes.push_back(tinyRecordDescriptor);
	smallRecordAttributes.push_back(smallRecordDescriptor);

	// Fill out the values of all the data
	for ( int i=0; i<numRecords; ++i )
	{
		int nanoInt = i;
		char tinyChar = (i % 93) + ' ';
		char smallChars[3] = {tinyChar, tinyChar, tinyChar};

		int tinyCount = 1;
		int smallCount = 3;

		memcpy(nanoRecords[i], &nanoInt, sizeof(int));
		
		memcpy(tinyRecords[i], &tinyCount, sizeof(int));
		memcpy(tinyRecords[i] + sizeof(int), &tinyChar, tinyCount*sizeof(char));

		memcpy(smallRecords[i], &smallCount, sizeof(int));
		memcpy(smallRecords[i] + sizeof(int), &smallChars, smallCount*sizeof(char));
	}

	// Insert records into the file, in a somewhat odd order (get it, odd, because it does i%2)
	for( int i=0; i<numRecords; ++i )
	{
		if (i%2 != 0)
		{
			rbfm->insertRecord(fileHandle, nanoRecordAttributes, nanoRecords[i], nanoRids[i]);
			rbfm->insertRecord(fileHandle, tinyRecordAttributes, tinyRecords[i], tinyRids[i]);
		}

		rbfm->insertRecord(fileHandle, smallRecordAttributes, smallRecords[i], smallRids[i]);
	}

	for( int i=0; i<numRecords; ++i )
	{
		if (i%2 == 0)
		{
			rbfm->insertRecord(fileHandle, nanoRecordAttributes, nanoRecords[i], nanoRids[i]);
			rbfm->insertRecord(fileHandle, tinyRecordAttributes, tinyRecords[i], tinyRids[i]);
		}
	}

	// Read the records back in
	for ( int i=numRecords - 1; i>=0; --i )
	{
		rbfm->readRecord(fileHandle, nanoRecordAttributes, nanoRids[i], nanoReturnedRecords[i]);
		rbfm->readRecord(fileHandle, tinyRecordAttributes, tinyRids[i], tinyReturnedRecords[i]);
		rbfm->readRecord(fileHandle, smallRecordAttributes, smallRids[i], smallReturnedRecords[i]);
	}

	// Verify the data is correct
	bool success = true;
	for( int i=0; i<numRecords; ++i )
	{
		if (memcmp(nanoRecords[i], nanoReturnedRecords[i], nanoSize))
		{
			std::cout << "testSmallRecords1() failed on nano[" << i << "]" << std::endl << std::endl;
			success = false;
			break;
		}

		if (memcmp(tinyRecords[i], tinyReturnedRecords[i], tinySize))
		{
			std::cout << "testSmallRecords1() failed on tiny[" << i << "]" << std::endl << std::endl;
			success = false;
			break;
		}

		if (memcmp(smallRecords[i], smallReturnedRecords[i], smallSize))
		{
			std::cout << "testSmallRecords1() failed on small[" << i << "]" << std::endl << std::endl;
			success = false;
			break;
		}
	}

	// Free up all the memory we allocated
	for( int i=0; i<numRecords; ++i )
	{
		free(nanoRecords[i]);
		free(nanoReturnedRecords[i]);

		free(tinyRecords[i]);
		free(tinyReturnedRecords[i]);

		free(smallRecords[i]);
		free(smallReturnedRecords[i]);
	}

	return success;
}

bool testSmallRecords2(FileHandle& fileHandle)
{
	return true;
}

void rbfmTest()
{
	unsigned numTests = 0;
    unsigned numPassed = 0;
    RC rc;

	cout << "RecordBasedFileManager tests" << endl;
	PagedFileManager *pfm = PagedFileManager::instance();
	RecordBasedFileManager *rbfm = RecordBasedFileManager::instance();
    FileHandle handle0, handle1, handle2;

	// Test creating many very small records
	TEST_FN_EQ( 0, pfm->createFile("testFile0.db"), "Create testFile0.db");
	TEST_FN_EQ( 0, pfm->openFile("testFile0.db", handle0), "Open testFile0.db and store in handle0");
	TEST_FN_EQ( true, testSmallRecords1(handle0), "Testing inserting many tiny records individually");
	TEST_FN_EQ( true, testSmallRecords2(handle0), "Testing inserting many tiny records in batches");

	// Test creating records with odd sizes
	TEST_FN_EQ( 0, pfm->createFile("testFile1.db"), "Create testFile1.db");
	TEST_FN_EQ( 0, pfm->openFile("testFile1.db", handle1), "Open testFile1.db and store in handle1");

	// Test closing/opening the file inbetween every operation
	TEST_FN_EQ( 0, pfm->createFile("testFile2.db"), "Create testFile2.db");
	TEST_FN_EQ( 0, pfm->openFile("testFile2.db", handle2), "Open testFile2.db and store in handle2");

	// Clean up
	TEST_FN_EQ( 0, pfm->closeFile(handle0), "Close handle0");
	TEST_FN_EQ( 0, pfm->closeFile(handle1), "Close handle1");
	TEST_FN_EQ( 0, pfm->closeFile(handle2), "Close handle2");
	TEST_FN_EQ( 0, pfm->destroyFile("testFile0.db"), "Destroy testFile0.db");
	TEST_FN_EQ( 0, pfm->destroyFile("testFile1.db"), "Destroy testFile1.db");
	TEST_FN_EQ( 0, pfm->destroyFile("testFile2.db"), "Destroy testFile2.db");

	cout << "\nRBFM Tests complete: " << numPassed << "/" << numTests << "\n\n" << endl;
	assert(numPassed == numTests);
}

// Super duper hacky way to access protected destructors !!
class DeletablePFM : public PagedFileManager { public: ~DeletablePFM() { } };
class DeletableRBFM : public RecordBasedFileManager { public: ~DeletableRBFM() { } };

void killPFM()
{
	DeletablePFM* pfm = reinterpret_cast<DeletablePFM*>(PagedFileManager::instance());

	// NOTE: Make sure ~PagedFileManager() has _pf_manager = NULL; in it!!!
	delete pfm;

	PagedFileManager::instance();
}

void killRBFM()
{
	DeletableRBFM* rbfm = reinterpret_cast<DeletableRBFM*>(RecordBasedFileManager::instance());

	// NOTE: Make sure ~RecordBasedFileManager() has _rbf_manager = NULL; in it!!!
	delete rbfm;

	RecordBasedFileManager::instance();
}

bool pfmKillProcTest1()
{
	killPFM();

	return true;
}

void pfmTest()
{
    unsigned numTests = 0;
    unsigned numPassed = 0;
    RC rc;

    cout << "PagedFileManager tests" << endl;
    PagedFileManager *pfm = PagedFileManager::instance();
    FileHandle handle0, handle1, handle1_copy, handle2;

    TEST_FN_EQ( 0, pfm->createFile("testFile0.db"), "Create testFile0.db");
    TEST_FN_EQ( 0, pfm->openFile("testFile0.db", handle0), "Open testFile0.db and store in handle0");
    TEST_FN_EQ( 0, pfm->closeFile(handle0), "Close handle0 (testFile0.db)");
    TEST_FN_EQ( 0, pfm->destroyFile("testFile0.db"), "Destroy testFile0.db");
    TEST_FN_NEQ(0, pfm->destroyFile("testFile1.db"), "Destroy testFile1.db (does not exist, should fail)");
    TEST_FN_EQ( 0, pfm->createFile("testFile1.db"), "Create testFile0.db");
    TEST_FN_NEQ(0, pfm->createFile("testFile1.db"), "Create testFile1.db (already exists, should fail)");
    TEST_FN_NEQ(0, pfm->closeFile(handle1), "Close handl1 (uninitialized, should fail)");
    TEST_FN_NEQ(0, pfm->openFile("testFile2.db", handle2), "Open testFile2.db and store in handle2 (does not exist, should fail)");
    TEST_FN_NEQ(0, pfm->closeFile(handle2), "Close handle2 (should never have been initialized, should fail)");
    TEST_FN_EQ( 0, pfm->openFile("testFile1.db", handle1), "Open testFile1.db and store in handle1");
    TEST_FN_NEQ(0, pfm->openFile("testFile2.db", handle1), "Open testFile2.db and store in handle2 (initialized, should fail)");
    TEST_FN_EQ( 0, pfm->openFile("testFile1.db", handle1_copy), "Open testFile1.db and store in handle1_copy");
    TEST_FN_EQ( 0, pfm->closeFile(handle1_copy), "Close handle1_copy");
    TEST_FN_NEQ(0, pfm->closeFile(handle1_copy), "Close handle1_copy (should fail, already closed)");
    TEST_FN_EQ( 0, pfm->openFile("testFile1.db", handle1_copy), "Open testFile1.db and store in recycled handle1_copy");
    TEST_FN_EQ( 0, pfm->closeFile(handle1), "Close handle1");
    TEST_FN_EQ( 0, pfm->closeFile(handle1_copy), "Close handle1_copy");
    TEST_FN_EQ( 0, pfm->destroyFile("testFile1.db"), "Destroy testFile1.db");
	TEST_FN_EQ( true, pfmKillProcTest1(), "Test some cases where the process 'exits' between some calls");

    cout << "\nPFM Tests complete: " << numPassed << "/" << numTests << "\n\n" << endl;
	assert(numPassed == numTests);
}

void fhTest()
{
    unsigned numTests = 0;
    unsigned numPassed = 0;
    RC rc;

    cout << "FileHandle tests" << endl;
    PagedFileManager *pfm = PagedFileManager::instance();

    ///// Do a bunch of persistent and non-persistent calls
    string fileName = "fh_test"; 
    rc = pfm->createFile(fileName.c_str());
    assert(rc == success);
    assert(FileExists(fileName.c_str()));
    FileHandle fileHandle;
    rc = pfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);
    unsigned count = fileHandle.getNumberOfPages();
    assert(count == (unsigned)0);
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);

    // Re-open, check pages, write to 1, write to 0, close
    rc = pfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);
    count = fileHandle.getNumberOfPages();
    assert(count == (unsigned)0);
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);    
}


// Function to prepare the data in the correct form to be inserted/read
void prepareRecord(const int nameLength, const string &name, const int age, const float height, const int salary, void *buffer, int *recordSize)
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
    
    *recordSize = offset;
}

void prepareLargeRecord(const int index, void *buffer, int *size)
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
    // 1. Create File
    cout << "****In RBF Test Case 1****" << endl;

    RC rc;
    string fileName = "test";

    // Create a file named "test"
    rc = pfm->createFile(fileName.c_str());
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
    rc = pfm->createFile(fileName.c_str());
    assert(rc != success);

    cout << "Test Case 1 Passed!" << endl << endl;
    return 0;
}


int RBFTest_2(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. Destroy File
    cout << "****In RBF Test Case 2****" << endl;

    RC rc;
    string fileName = "test";

    rc = pfm->destroyFile(fileName.c_str());
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
    // 1. Create File
    // 2. Open File
    // 3. Get Number Of Pages
    // 4. Close File
    cout << "****In RBF Test Case 3****" << endl;

    RC rc;
    string fileName = "test_1";

    // Create a file named "test_1"
    rc = pfm->createFile(fileName.c_str());
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
    rc = pfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Get the number of pages in the test file
    unsigned count = fileHandle.getNumberOfPages();
    assert(count == (unsigned)0);

    // Close the file "test_1"
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);

    cout << "Test Case 3 Passed!" << endl << endl;

    return 0;
}



int RBFTest_4(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. Open File
    // 2. Append Page
    // 3. Get Number Of Pages
    // 3. Close File
    cout << "****In RBF Test Case 4****" << endl;

    RC rc;
    string fileName = "test_1";

    // Open the file "test_1"
    FileHandle fileHandle;
    rc = pfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Append the first page
    void *data = malloc(PAGE_SIZE);
    for(unsigned i = 0; i < PAGE_SIZE; i++)
    {
        *((char *)data+i) = i % 94 + 32;
    }
    rc = fileHandle.appendPage(data);
    assert(rc == success);
   
    // Get the number of pages
    unsigned count = fileHandle.getNumberOfPages();
    assert(count == (unsigned)1);

    // Close the file "test_1"
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);

    free(data);

    cout << "Test Case 4 Passed!" << endl << endl;

    return 0;
}


int RBFTest_5(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. Open File
    // 2. Read Page
    // 3. Close File
    cout << "****In RBF Test Case 5****" << endl;

    RC rc;
    string fileName = "test_1";

    // Open the file "test_1"
    FileHandle fileHandle;
    rc = pfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Read the first page
    void *buffer = malloc(PAGE_SIZE);
    rc = fileHandle.readPage(0, buffer);
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
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);

    free(data);
    free(buffer);

    cout << "Test Case 5 Passed!" << endl << endl;

    return 0;
}


int RBFTest_6(PagedFileManager *pfm)
{
    // Functions Tested:
    // 1. Open File
    // 2. Write Page
    // 3. Read Page
    // 4. Close File
    // 5. Destroy File
    cout << "****In RBF Test Case 6****" << endl;

    RC rc;
    string fileName = "test_1";

    // Open the file "test_1"
    FileHandle fileHandle;
    rc = pfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Update the first page
    void *data = malloc(PAGE_SIZE);
    for(unsigned i = 0; i < PAGE_SIZE; i++)
    {
        *((char *)data+i) = i % 10 + 32;
    }
    rc = fileHandle.writePage(0, data);
    assert(rc == success);

    // Read the page
    void *buffer = malloc(PAGE_SIZE);
    rc = fileHandle.readPage(0, buffer);
    assert(rc == success);

    // Check the integrity
    rc = memcmp(data, buffer, PAGE_SIZE);
    assert(rc == success);
 
    // Close the file "test_1"
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);

    free(data);
    free(buffer);

    // Destroy File
    rc = pfm->destroyFile(fileName.c_str());
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
    // 1. Create File
    // 2. Open File
    // 3. Append Page
    // 4. Get Number Of Pages
    // 5. Read Page
    // 6. Write Page
    // 7. Close File
    // 8. Destroy File
    cout << "****In RBF Test Case 7****" << endl;

    RC rc;
    string fileName = "test_2";

    // Create the file named "test_2"
    rc = pfm->createFile(fileName.c_str());
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
    rc = pfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    // Append 50 pages
    void *data = malloc(PAGE_SIZE);
    for(unsigned j = 0; j < 50; j++)
    {
        for(unsigned i = 0; i < PAGE_SIZE; i++)
        {
            *((char *)data+i) = i % (j+1) + 32;
        }
        rc = fileHandle.appendPage(data);
        assert(rc == success);
    }
    cout << "50 Pages have been successfully appended!" << endl;
   
    // Get the number of pages
    unsigned count = fileHandle.getNumberOfPages();
    assert(count == (unsigned)50);

    // Read the 25th page and check integrity
    void *buffer = malloc(PAGE_SIZE);
    rc = fileHandle.readPage(24, buffer);
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
    rc = fileHandle.writePage(24, data);
    assert(rc == success);

    // Read the 25th page and check integrity
    rc = fileHandle.readPage(24, buffer);
    assert(rc == success);
    
    rc = memcmp(buffer, data, PAGE_SIZE);
    assert(rc == success);

    // Close the file "test_2"
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);

    // Destroy File
    rc = pfm->destroyFile(fileName.c_str());
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

int RBFTest_8(RecordBasedFileManager *rbfm) {
    // Functions tested
    // 1. Create Record-Based File
    // 2. Open Record-Based File
    // 3. Insert Record
    // 4. Read Record
    // 5. Close Record-Based File
    // 6. Destroy Record-Based File
    cout << "****In RBF Test Case 8****" << endl;
   
    RC rc;
    string fileName = "test_3";

    // Create a file named "test_3"
    rc = rbfm->createFile(fileName.c_str());
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
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);
   
      
    RID rid; 
    int recordSize = 0;
    void *record = malloc(100);
    void *returnedData = malloc(100);

    vector<Attribute> recordDescriptor;
    createRecordDescriptor(recordDescriptor);
    
    // Insert a record into a file
    prepareRecord(6, "Peters", 24, 170.1f, 5000, record, &recordSize);
    cout << "Insert Data:" << endl;
    rbfm->printRecord(recordDescriptor, record);
    
    rc = rbfm->insertRecord(fileHandle, recordDescriptor, record, rid);
    assert(rc == success);
    
    // Given the rid, read the record from file
    rc = rbfm->readRecord(fileHandle, recordDescriptor, rid, returnedData);
    assert(rc == success);

#ifndef REDIRECT_PRINT_RECORD
    // Ignore this spammy message when we're redirecting directly to file
    cout << "Returned Data:" << endl;
#endif
    rbfm->printRecord(recordDescriptor, returnedData);

    // Compare whether the two memory blocks are the same
    if(memcmp(record, returnedData, recordSize) != 0)
    {
       cout << "Test Case 8 Failed!" << endl << endl;
        free(record);
        free(returnedData);
        return -1;
    }
    
    // Close the file "test_3"
    rc = rbfm->closeFile(fileHandle);
    assert(rc == success);

    // Destroy File
    rc = rbfm->destroyFile(fileName.c_str());
    assert(rc == success);
    
    free(record);
    free(returnedData);
    cout << "Test Case 8 Passed!" << endl << endl;
    
    return 0;
}

int RBFTest_9(RecordBasedFileManager *rbfm, vector<RID> &rids, vector<int> &sizes) {
    // Functions tested
    // 1. Create Record-Based File
    // 2. Open Record-Based File
    // 3. Insert Multiple Records
    // 4. Close Record-Based File
    cout << "****In RBF Test Case 9****" << endl;
   
    RC rc;
    string fileName = "test_4";

    // Create a file named "test_4"
    rc = rbfm->createFile(fileName.c_str());
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
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);


    RID rid; 
    void *record = malloc(1000);
    int numRecords = 2000;
    // int numRecords = 500;

    vector<Attribute> recordDescriptor;
    createLargeRecordDescriptor(recordDescriptor);

    for(unsigned i = 0; i < recordDescriptor.size(); i++)
    {
        cout << "Attribute Name: " << recordDescriptor[i].name << endl;
        cout << "Attribute Type: " << (AttrType)recordDescriptor[i].type << endl;
        cout << "Attribute Length: " << recordDescriptor[i].length << endl << endl;
    }

    // Insert 2000 records into file
    for(int i = 0; i < numRecords; i++)
    {
        // Test insert Record
        int size = 0;
        memset(record, 0, 1000);
        prepareLargeRecord(i, record, &size);

        rc = rbfm->insertRecord(fileHandle, recordDescriptor, record, rid);
        assert(rc == success);

        rids.push_back(rid);
        sizes.push_back(size);        
    }
    // Close the file "test_4"
    rc = rbfm->closeFile(fileHandle);
    assert(rc == success);

    free(record);    
    cout << "Test Case 9 Passed!" << endl << endl;

    return 0;
}

int RBFTest_10(RecordBasedFileManager *rbfm, vector<RID> &rids, vector<int> &sizes) {
    // Functions tested
    // 1. Open Record-Based File
    // 2. Read Multiple Records
    // 3. Close Record-Based File
    // 4. Destroy Record-Based File
    cout << "****In RBF Test Case 10****" << endl;
   
    RC rc;
    string fileName = "test_4";

    // Open the file "test_4"
    FileHandle fileHandle;
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);
    
    int numRecords = 2000;
    void *record = malloc(1000);
    void *returnedData = malloc(1000);

    vector<Attribute> recordDescriptor;
    createLargeRecordDescriptor(recordDescriptor);
    
    for(int i = 0; i < numRecords; i++)
    {
        memset(record, 0, 1000);
        memset(returnedData, 0, 1000);
        rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], returnedData);
        assert(rc == success);
        
#ifndef REDIRECT_PRINT_RECORD
        // Ignore this spammy message when we're redirecting directly to file
        cout << "Returned Data:" << endl;
#endif
        rbfm->printRecord(recordDescriptor, returnedData);

        int size = 0;
        prepareLargeRecord(i, record, &size);
        if(memcmp(returnedData, record, sizes[i]) != 0)
        {
            cout << "Test Case 10 Failed!" << endl << endl;
            free(record);
            free(returnedData);
            return -1;
        }
    }
    
    // Close the file "test_4"
    rc = rbfm->closeFile(fileHandle);
    assert(rc == success);
    
    rc = rbfm->destroyFile(fileName.c_str());
    assert(rc == success);

    if(!FileExists(fileName.c_str())) {
        cout << "File " << fileName << " has been destroyed." << endl << endl;
        free(record);
        free(returnedData);
        cout << "Test Case 10 Passed!" << endl << endl;
        return 0;
    }
    else {
        cout << "Failed to destroy file!" << endl;
        cout << "Test Case 10 Failed!" << endl << endl;
        free(record);
        free(returnedData);
        return -1;
    }
}

int RBFTest_11(RecordBasedFileManager *rbfm, vector<RID> &rids, vector<int> &sizes) {
    // Functions tested
    // 1. Create Record-Based File
    // 2. Open Record-Based File
    // 3. Insert Multiple Records
    // 4. Close Record-Based File
    cout << "****In RBF Test Case 11****" << endl;
   
    RC rc;
    string fileName = "our_test_5";

    // Create a file named "our_test_4"
    rc = rbfm->createFile(fileName.c_str());
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

    // Open the file "our_test_4"
    FileHandle fileHandle;
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);


    RID rid; 
    void *record = malloc(1000);
    void *record_copy = malloc(1000);
    int numRecords = 10;

    vector<Attribute> recordDescriptor;
    createLargeRecordDescriptor(recordDescriptor);

    for(unsigned i = 0; i < recordDescriptor.size(); i++)
    {
        cout << "Attribute Name: " << recordDescriptor[i].name << endl;
        cout << "Attribute Type: " << (AttrType)recordDescriptor[i].type << endl;
        cout << "Attribute Length: " << recordDescriptor[i].length << endl << endl;
    }

    // Insert 2000 records into file
    for(int i = 0; i < numRecords; i++)
    {
        // Test insert Record
        int size = 0;
        memset(record, 0, 1000);
        memset(record_copy, 0, 1000);
        prepareLargeRecord(i, record, &size);

        // Write, read, and then immediately compare the two
        rc = rbfm->insertRecord(fileHandle, recordDescriptor, record, rid);
        assert(rc == success);
        rc = rbfm->readRecord(fileHandle, recordDescriptor, rid, record_copy);
        assert(rc == success);
        assert(memcmp(record, record_copy, size) == 0);
        rbfm->printRecord(recordDescriptor, record_copy);

        rids.push_back(rid);
        sizes.push_back(size);        
    }
    // Close the file "test_4"
    rc = rbfm->closeFile(fileHandle);
    assert(rc == success);

    free(record);    
    cout << "Test Case 11 Passed!" << endl << endl;

    return 0;
}

void cleanup()
{
	remove("test");
    remove("test_1");
    remove("test_2");
    remove("test_3");
    remove("test_4");
    remove("our_test_5");
    remove("testFile0.db");
    remove("testFile1.db");
    remove("testFile2.db");
}

int main()
{
    PagedFileManager *pfm = PagedFileManager::instance(); // To test the functionality of the paged file manager
    RecordBasedFileManager *rbfm = RecordBasedFileManager::instance(); // To test the functionality of the record-based file manager
    
    cleanup();
    
    RBFTest_1(pfm);
    RBFTest_2(pfm); 
    RBFTest_3(pfm);
    RBFTest_4(pfm);
    RBFTest_5(pfm); 
    RBFTest_6(pfm);
    RBFTest_7(pfm);
    RBFTest_8(rbfm);
    
    vector<RID> rids;
    vector<int> sizes;
    RBFTest_9(rbfm, rids, sizes);
    RBFTest_10(rbfm, rids, sizes);

    // Our tests...
    RBFTest_11(rbfm, rids, sizes);

	pfmTest();
	rbfmTest();

    cleanup();

    return 0;
}
