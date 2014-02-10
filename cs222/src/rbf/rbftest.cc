#include <iostream>
#include <string>
#include <sstream>
#include <cassert>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <cstdlib>

#include "pfm.h"
#include "rbfm.h"
#include "../util/returncodes.h"

using namespace std;

const int success = 0;

// Check if a file exists
bool FileExists(string fileName)
{
    struct stat stFileInfo;

    if(stat(fileName.c_str(), &stFileInfo) == 0) return true;
    else return false;
}

// Simple macros to test equality and increment counters to keep track of passing tests
#define TEST_FN_PREFIX ++numTests;
#define TEST_FN_POSTFIX(msg) { ++numPassed; cout << ' ' << numTests << ") OK: " << msg << endl; } \
                        else { cout << ' ' << numTests << ") FAIL: " << msg << "<" << rc::rcToString(rc) << ">" << endl; }

#define TEST_FN_EQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) == expected) TEST_FN_POSTFIX(msg)
#define TEST_FN_NEQ(expected,fn,msg) TEST_FN_PREFIX if((rc=(fn)) != expected) TEST_FN_POSTFIX(msg)

#define ALL_MIN_SKIP 2
#define ALL_MAX_SKIP 50
#define REORGANIZE_MIN_SKIP 2
#define REORGANIZE_MAX_SKIP 50
#define UPDATE_MIN_SKIP 1
#define UPDATE_MAX_SKIP 50

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

// Super duper hacky way to access protected destructors !!
class DeletablePFM : public PagedFileManager { public: ~DeletablePFM() { } };
class DeletableRBFM : public RecordBasedFileManager { public: ~DeletableRBFM() { } };

void killPFM()
{
	// NOTE: Make sure ~PagedFileManager() has _pf_manager = NULL; in it!!!
	delete reinterpret_cast<DeletablePFM*>(PagedFileManager::instance());
	PagedFileManager::instance();
}

void killRBFM()
{
	// NOTE: Make sure ~RecordBasedFileManager() has _rbf_manager = NULL; in it!!!
	delete reinterpret_cast<DeletableRBFM*>(RecordBasedFileManager::instance());
	RecordBasedFileManager::instance();
}

bool testSmallRecords1(FileHandle& fileHandle)
{
	int numRecords = 10000;
	RecordBasedFileManager *rbfm = RecordBasedFileManager::instance();
	
	Attribute nanoRecordDescriptor;		nanoRecordDescriptor.name = "Nano";		nanoRecordDescriptor.type = TypeInt;		nanoRecordDescriptor.length = (AttrLength)4;
	Attribute tinyRecordDescriptor;		tinyRecordDescriptor.name = "Tiny";		tinyRecordDescriptor.type = TypeVarChar;	tinyRecordDescriptor.length = (AttrLength)1;
	Attribute smallRecordDescriptor;	smallRecordDescriptor.name = "Small";	smallRecordDescriptor.type = TypeVarChar;	smallRecordDescriptor.length = (AttrLength)3;

	std::vector<RID> nanoRids;
	std::vector<char*> nanoRecords;
	std::vector<char*> nanoReturnedRecords;

	std::vector<RID> tinyRids;
	std::vector<char*> tinyRecords;
	std::vector<char*> tinyReturnedRecords;

	std::vector<RID> smallRids;
	std::vector<char*> smallRecords;
	std::vector<char*> smallReturnedRecords;

	size_t nanoSize = nanoRecordDescriptor.length;
	size_t tinySize = tinyRecordDescriptor.length + sizeof(int);
	size_t smallSize = smallRecordDescriptor.length + sizeof(int);

	// We're just going to repeat these 3 simple records over and over
	std::vector<Attribute> nanoRecordAttributes;
	std::vector<Attribute> tinyRecordAttributes;
	std::vector<Attribute> smallRecordAttributes;

	nanoRecordAttributes.push_back(nanoRecordDescriptor);
	tinyRecordAttributes.push_back(tinyRecordDescriptor);
	smallRecordAttributes.push_back(smallRecordDescriptor);

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

	for( int i=numRecords-1; i>=0; --i )
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

RC testSmallRecords2(FileHandle& fileHandle)
{
	// Our assortment of types of records
	Attribute aInt4;	aInt4.name = "aInt1";	aInt4.type = TypeInt;		aInt4.length = (AttrLength)4;
	Attribute aFlt4;	aFlt4.name = "aFlt1";	aFlt4.type = TypeReal;		aFlt4.length = (AttrLength)4;
	Attribute aChr4;	aChr4.name = "aChr4";	aChr4.type = TypeVarChar;	aChr4.length = (AttrLength)4;

	std::vector< std::vector< Attribute > > attributeLists;
	std::vector<void*> buffersIn;
	std::vector<void*> buffersOut;
	std::vector<int> shuffledOrdering;
	std::vector<RID> rids;
	std::vector<int> sizes;

	RC ret;
	const int numRecords = 12345;

	for (int i=0; i<numRecords; ++i)
	{
		shuffledOrdering.push_back(i);
		rids.push_back(RID());

		attributeLists.push_back(std::vector<Attribute>());
		std::vector<Attribute>& attributes = attributeLists.back();

		// Select from a variation of different sizes of records
		int size = 0;
		switch(i%5)
		{
		case 0:
			attributes.push_back(aInt4);
			attributes.push_back(aChr4);
			size = (aInt4.length + sizeof(char) * aChr4.length + sizeof(int));
			break;

		case 1:
			attributes.push_back(aFlt4);
			size = (aFlt4.length);
			break;

		case 2:
			attributes.push_back(aFlt4);
			attributes.push_back(aInt4);
			size = (aFlt4.length + aInt4.length);
			break;

		case 3:
			attributes.push_back(aFlt4);
			attributes.push_back(aFlt4);
			attributes.push_back(aFlt4);
			attributes.push_back(aFlt4);
			size = (4 * aFlt4.length);
			break;

		case 4:
			attributes.push_back(aInt4);
			size = (aInt4.length);
			break;
		}

		// Allocate memory
		sizes.push_back(size);
		buffersIn.push_back(malloc(size));
		buffersOut.push_back(malloc(size));

		int intData;
		int countData;
		float floatData;
		float float4Data[4];
		char charData[4];
		char* buffer = (char*)buffersIn.back();

		// Write out data
		switch(i%5)
		{
		case 0:
			intData = i;
			countData = 4;
			charData[0] = 'a' + i%50;
			charData[1] = 'a';
			charData[2] = 'a';
			charData[3] = 'a';
			memcpy(buffer, &intData, sizeof(int));
			memcpy(buffer + sizeof(int), &countData, sizeof(int));
			memcpy(buffer + 2 * sizeof(int), charData, 4 * sizeof(char));
		break;

		case 1:
			floatData = i * 1.25f;
			memcpy(buffer, &floatData, sizeof(float));
		break;

		case 2:
			floatData = i / 2134.56789f;
			intData = i * 2;
			memcpy(buffer, &floatData, sizeof(float));
			memcpy(buffer + sizeof(float), &intData, sizeof(int));
			break;

		case 3:
			float4Data[0] = float4Data[1] = float4Data[2] = float4Data[3] = i * 3.14f;
			memcpy(buffer, float4Data, 4 * sizeof(float));
			break;

		case 4:
			intData = i * 7;
			memcpy(buffer, &intData, sizeof(int));
		}
	}

	// Shuffle indices so that we can insert in a random order
	RecordBasedFileManager* rbfm = RecordBasedFileManager::instance();
	std::random_shuffle(shuffledOrdering.begin(), shuffledOrdering.end());
	for (std::vector<int>::const_iterator itr = shuffledOrdering.begin(); itr != shuffledOrdering.end(); ++itr)
	{
		int i = *itr;

		ret = rbfm->insertRecord(fileHandle, attributeLists[i], buffersIn[i], rids[i]);
		if (ret != rc::OK)
		{
			std::cout << "Failed to insert shuffled record[" << i << "]" << std::endl;
			break;
		}
	}
	
	// Go through all records and verify memory is correctly read, backwards for some reason
	if (ret == rc::OK)
	{
		for (int i=numRecords-1; i>=0; --i)
		{
			ret = rbfm->readRecord(fileHandle, attributeLists[i], rids[i], buffersOut[i]);
			if (ret != rc::OK)
			{
				std::cout << "Failed to read shuffled record[" << i << "]" << std::endl;
				break;
			}

			if (memcmp(buffersIn[i], buffersOut[i], sizes[i]))
			{
				std::cout << "Failed to read correct data of shuffled record[" << i << "]" << std::endl;
                ret = rc::RECORD_CORRUPT;
				break;
			}
		}
	}

	// Finally free up memory
	for (int i=numRecords-1; i>=0; --i)
	{
		free(buffersIn[i]);
		free(buffersOut[i]);
	}

    return ret;
}

RC testMaxSizeRecords(FileHandle& fileHandle, int recordSizeDelta, int minRecordSize, bool insertForwards)
{
    std::vector<char*> buffersIn;
    std::vector<char*> buffersOut;
    std::vector<RID> rids;
    std::vector< Attribute > recordDescriptor;
    Attribute bigString;
    bigString.name = "Big String";
    bigString.type = TypeVarChar;
    recordDescriptor.push_back(bigString);

    const int maxRecordSize = PAGE_SIZE - sizeof(PageIndexSlot) - sizeof(PageIndexHeader) - sizeof(unsigned) * recordDescriptor.size() - sizeof(unsigned) - sizeof(unsigned);

    // Allocate memory
    int seed = 0x7ed55d16;
    for (int size = minRecordSize, index = 0; size <= maxRecordSize; size += recordSizeDelta, ++index)
    {
        rids.push_back(RID());
        buffersIn.push_back((char*)malloc(size));
        buffersOut.push_back((char*)malloc(size));

        char* buffer = buffersIn.back();

        recordDescriptor.back().length = size - sizeof(int);

        // First write out the size of the string, then fill it with data
        memcpy(buffer, &recordDescriptor.back().length, sizeof(int));
        for (int i=sizeof(int); i<size; ++i)
        {
            seed *= (i * 0xfd7046c5) + (i << 24); // not a real hash, do not use for actual hashing!
            char c = ' ' + seed % 90;
            buffer[i] = c;
        }
    }

    // Do inserts
    RC ret;
    RecordBasedFileManager* rbfm = RecordBasedFileManager::instance();
    if (insertForwards)
    {
        for (int size = minRecordSize, index = 0; size <= maxRecordSize; size += recordSizeDelta, ++index)
        {
            recordDescriptor.back().length = size - sizeof(int);
            ret = rbfm->insertRecord(fileHandle, recordDescriptor, buffersIn[index], rids[index]);
        }
    }
    else
    {
        for (int size = maxRecordSize, index = rids.size() - 1; size >= minRecordSize; size -= recordSizeDelta, --index)
        {
            recordDescriptor.back().length = size - sizeof(int);
            ret = rbfm->insertRecord(fileHandle, recordDescriptor, buffersIn[index], rids[index]);
        }
    }

    // Check data was not harmed
    if (ret == rc::OK)
    {
        for (int size = minRecordSize, index = 0; size <= maxRecordSize; size += recordSizeDelta, ++index)
        {
            recordDescriptor.back().length = size - sizeof(int);
            ret = rbfm->readRecord(fileHandle, recordDescriptor, rids[index], buffersOut[index]);

            if (ret == rc::OK && memcmp(buffersIn[index], buffersOut[index], size))
            {
                std::cout << "There was an error checking the big string[" << index << "], size=" << size << std::endl;
                ret = rc::FILE_CORRUPT;
                break;
            }
        }
    }

    // Clean up memory
    for (int size = minRecordSize, index = 0; size <= maxRecordSize; size += recordSizeDelta, ++index)
    {
        free(buffersIn[index]);
        free(buffersOut[index]);
    }

    return ret;
}

Attribute randomAttribute()
{
    Attribute attr;
    switch(rand() % 3)
    {
    case 0:
        attr.name = "Random-Int";
        attr.type = TypeInt;
        attr.length = sizeof(int);
        break;

    case 1:
        attr.name = "Random-Real";
        attr.type = TypeReal;
        attr.length = sizeof(float);
        break;

    case 2:
        attr.name = "Random-VarChar";
        attr.type = TypeVarChar;
        attr.length = 1 + rand() % 17;
        break;
    }

    return attr;
}

void mallocRandomValue(Attribute& attr, int& size, void** bufferIn, void** bufferOut)
{
    switch(attr.type)
    {
    case TypeInt:
    {
        size = sizeof(int);
        *bufferIn = malloc(size);
        *bufferOut = malloc(size);

        int* val = (int*)(*bufferIn);
        *val = rand();
        break;
    }

    case TypeReal:
    {
        size = sizeof(float);
        *bufferIn = malloc(size);
        *bufferOut = malloc(size);

        float* val = (float*)(*bufferIn);
        *val = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
        break;
    }

    case TypeVarChar:
    {
        int stringLen = rand() % attr.length;
        size = sizeof(int) + stringLen;
        *bufferIn = malloc(size);
        *bufferOut = malloc(size);

        memcpy(*bufferIn, &stringLen, sizeof(int));
        char* str = (char*)*bufferIn + sizeof(int);
        for (int i=0; i<stringLen; ++i)
        {
            *str = ' ' + (rand() % 90);
        }
        break;
    }
    }
}

RC testRandomInsertion(FileHandle& fileHandle, int numIterations, int minRecords, int maxRecords, bool doDeletes)
{
    RC ret;
    RecordBasedFileManager* rbfm = RecordBasedFileManager::instance();

    std::vector< std::vector< Attribute > > attributeLists;
    std::vector< std::vector< int > > bufferSizeLists;
    std::vector< std::vector< void* > > bufferInLists;
    std::vector< std::vector< void* > > bufferOutLists;
    std::vector< RID > ridLists;
	std::vector< unsigned > indexes;

    // Generate data
    for (int iteration = 0; iteration < numIterations; ++iteration)
    {
        attributeLists.push_back(std::vector< Attribute >());
        bufferSizeLists.push_back(std::vector< int >());
        bufferInLists.push_back(std::vector< void* >());
        bufferOutLists.push_back(std::vector< void* >());
        ridLists.push_back(RID());
		indexes.push_back(iteration);

        std::vector< Attribute >& attributeList = attributeLists.back();
        std::vector< int >& bufferSizes = bufferSizeLists.back();
        std::vector< void* >& buffersIn = bufferInLists.back();
        std::vector< void* >& buffersOut = bufferOutLists.back();

        int numInserts = minRecords + (rand() % (maxRecords-minRecords));
        for (int insert = 0; insert < numInserts; ++insert)
        {
            attributeList.push_back(randomAttribute());
            buffersIn.push_back(NULL);
            buffersOut.push_back(NULL);
            bufferSizes.push_back(0);

            mallocRandomValue(attributeList.back(), bufferSizes.back(), &buffersIn.back(), &buffersOut.back());
        }
    }

    // Insert records
    std::vector< void* > unifiedBuffersIn;
    std::vector< void* > unifiedBuffersOut;
    std::vector< int > unifiedBufferSizes;
    for (int iteration = 0; iteration < numIterations; ++iteration)
    {
        std::vector< Attribute >& attributeList = attributeLists[iteration];
        std::vector< void* >& buffersIn = bufferInLists[iteration];
        std::vector< int >& bufferSizes = bufferSizeLists[iteration];
        RID& rid = ridLists[iteration];

        // Calculate the size required for the unified record buffer
        int bufferSize = 0;
        int numInserts = bufferSizes.size();
        for (int insert = 0; insert < numInserts; ++insert)
        {
            bufferSize += bufferSizes[insert];
        }

        unifiedBufferSizes.push_back(bufferSize);
        unifiedBuffersIn.push_back(malloc(bufferSize));
        unifiedBuffersOut.push_back(malloc(bufferSize));

        // Fill up our unified record buffer
        int offset = 0;
        char* bufferIn = (char*)unifiedBuffersIn.back();
        char* bufferOut = (char*)unifiedBuffersOut.back();
        for (int insert = 0; insert < numInserts; ++insert)
        {
            memset(bufferOut, 0, bufferSizes[insert]);
            memcpy(bufferIn + offset, buffersIn[insert], bufferSizes[insert]);
            offset += bufferSizes[insert];
        }

        // Write out the record
        ret = rbfm->insertRecord(fileHandle, attributeList, bufferIn, rid);
        if (ret != rc::OK)
        {
            std::cout << "Error writing out record iteration=" << iteration << std::endl;
            break;
        }
    }

    // Read back records
    if (ret == rc::OK)
    {
        for (int iteration = 0; iteration < numIterations; ++iteration)
        {
            std::vector< Attribute >& attributeList = attributeLists[iteration];
            void* bufferOut = unifiedBuffersOut[iteration];
            RID& rid = ridLists[iteration];

            ret = rbfm->readRecord(fileHandle, attributeList, rid, bufferOut);
            if (ret != rc::OK)
            {
                std::cout << "Error reading in record iteration=" << iteration << std::endl;
                break;
            }
        }
    }

    // Verify records
    if (ret == rc::OK)
    {
        for (int iteration = 0; iteration < numIterations; ++iteration)
        {
            void* bufferIn = unifiedBuffersIn[iteration];
            void* bufferOut = unifiedBuffersOut[iteration];
            if (memcmp(bufferIn, bufferOut, unifiedBufferSizes[iteration]))
            {
                ret = rc::FILE_CORRUPT;
                std::cout << "Error verifying read back data iteration=" << iteration << std::endl;
                break;
            }
        }
    }

	// Delete some records and do rechecks on the rest
	if (doDeletes)
	{
		std::random_shuffle(indexes.begin(), indexes.end());

		// Continually delete half of the records and recheck the rest
		while(indexes.size() > 3)
		{
			for (unsigned numToDelete = 0; numToDelete <= indexes.size()/2; ++numToDelete)
			{
				unsigned toDelete = indexes.back(); indexes.pop_back();
				ret = rbfm->deleteRecord(fileHandle, attributeLists[toDelete], ridLists[toDelete]);
				if (ret != rc::OK)
				{
					std::cout << "Error deleting record=" << toDelete << std::endl;
					break;
				}
			}

			// Recheck remaining recs
			for (std::vector<unsigned>::const_iterator it = indexes.begin(); it != indexes.end(); ++it)
			{
				unsigned index = *it;
				std::vector< Attribute >& attributeList = attributeLists[index];
				void* bufferOut = unifiedBuffersOut[index];
				RID& rid = ridLists[index];

				ret = rbfm->readRecord(fileHandle, attributeList, rid, bufferOut);
				if (ret != rc::OK)
				{
					std::cout << "Error reading in record iteration=" << index << std::endl;
					break;
				}
			}
		}
	}

    // Free up memory
    for(int iteration = 0; iteration < numIterations; ++iteration)
    {
        free(unifiedBuffersIn[iteration]);
        free(unifiedBuffersOut[iteration]);

        std::vector< void* >& buffersIn = bufferInLists[iteration];
        std::vector< void* >& buffersOut = bufferOutLists[iteration];
        for (size_t insert = 0; insert < buffersIn.size(); ++insert)
        {
            free(buffersIn[insert]);
            free(buffersOut[insert]);
        }
    }

    return ret;
}

void rbfmTest()
{
	unsigned numTests = 0;
    unsigned numPassed = 0;
    RC rc;

	cout << "RecordBasedFileManager tests" << endl;
	PagedFileManager *pfm = PagedFileManager::instance();
    FileHandle handle0, handle1, handle2, handle3, handle4, handle5;

	remove("testFile0.db");
	remove("testFile1.db");
	remove("testFile2.db");
    remove("testFile3.db");
    remove("testFile4.db");
    remove("testFile5.db");

	// Test creating many very small records
	TEST_FN_EQ( 0, pfm->createFile("testFile0.db"), "Create testFile0.db");
	TEST_FN_EQ( 0, pfm->openFile("testFile0.db", handle0), "Open testFile0.db and store in handle0");
	TEST_FN_EQ( true, testSmallRecords1(handle0), "Testing inserting many tiny records individually");

	TEST_FN_EQ( 0, pfm->createFile("testFile1.db"), "Create testFile1.db");
	TEST_FN_EQ( 0, pfm->openFile("testFile1.db", handle1), "Open testFile1.db and store in handle1");
	TEST_FN_EQ( rc::OK, testSmallRecords2(handle1), "Testing inserting many tiny records in batches");

    // Test creating large records (close to PAGE_FILE size)
    TEST_FN_EQ( 0, pfm->createFile("testFile2.db"), "Create testFile2.db");
    TEST_FN_EQ( 0, pfm->openFile("testFile2.db", handle2), "Open testFile2.db and store in handle2");
    TEST_FN_EQ( rc::OK, testMaxSizeRecords(handle2, 1, 9 * PAGE_SIZE / 10, true), "Testing insertion of large records");

    // Test creation of varying sized records that should fill up any freespace lists
    TEST_FN_EQ( 0, pfm->createFile("testFile3.db"), "Create testFile3.db");
    TEST_FN_EQ( 0, pfm->openFile("testFile3.db", handle3), "Open testFile3.db and store in handle3");
    TEST_FN_EQ( rc::OK, testMaxSizeRecords(handle3, 257, sizeof(int) + sizeof(char), false), "Testing insertion of varying records");

    // Test creation of a very small record followed by a large record
    // With our freespace offsets, this is specifically designed to ensure we can insert in a bucket with a page already in it
    TEST_FN_EQ( 0, pfm->createFile("testFile4.db"), "Create testFile4.db");
    TEST_FN_EQ( 0, pfm->openFile("testFile4.db", handle4), "Open testFile4.db and store in handle4");
    TEST_FN_EQ( rc::OK, testMaxSizeRecords(handle4, 3000, sizeof(int) + sizeof(char), true), "Testing insertion of varying records");

    // Test creating randomly sized records
    TEST_FN_EQ( 0, pfm->createFile("testFile5.db"), "Create testFile5.db");
    TEST_FN_EQ( 0, pfm->openFile("testFile5.db", handle5), "Open testFile5.db and store in handle5");
    TEST_FN_EQ( rc::OK, testRandomInsertion(handle4, 1, 1, 2, false), "Testing insertion of randomly sized tiny records");
    TEST_FN_EQ( rc::OK, testRandomInsertion(handle4, 20, 1, 40, false), "Testing insertion of randomly sized medium records");
    TEST_FN_EQ( rc::OK, testRandomInsertion(handle4, 66, 1, 80, false), "Testing insertion of randomly sized large records");
    TEST_FN_EQ( rc::OK, testRandomInsertion(handle4, 333, 1, 160, false), "Testing insertion of randomly sized huge records");

	// Test opening and closing of files of files
	TEST_FN_EQ( 0, pfm->closeFile(handle0), "Close handle0");
	TEST_FN_EQ( 0, pfm->closeFile(handle1), "Close handle1");
    TEST_FN_EQ( 0, pfm->closeFile(handle2), "Close handle2");
    TEST_FN_EQ( 0, pfm->closeFile(handle3), "Close handle3");
    TEST_FN_EQ( 0, pfm->closeFile(handle4), "Close handle4");
    TEST_FN_EQ( 0, pfm->closeFile(handle5), "Close handle5");
	TEST_FN_EQ( 0, pfm->openFile("testFile4.db", handle1), "Open testFile4.db and store in handle1");
	TEST_FN_EQ( rc::FILE_HANDLE_ALREADY_INITIALIZED, pfm->openFile("testFile0.db", handle1), "Open testFile0.db and store in already initialized file handle");
	TEST_FN_EQ( 0, pfm->openFile("testFile4.db", handle2), "Open testFile4.db and store in handle2");
	TEST_FN_EQ( 0, pfm->openFile("testFile4.db", handle3), "Open testFile4.db and store in handle3");
	TEST_FN_EQ( 0, pfm->openFile("testFile4.db", handle4), "Open testFile4.db and store in handle4");
	TEST_FN_EQ( rc::FILE_COULD_NOT_DELETE, pfm->destroyFile("testFile4.db"), "Delete testFile4.db with open handles");
	TEST_FN_EQ( 0, pfm->closeFile(handle1), "Close testFile1.db");
	TEST_FN_EQ( rc::FILE_HANDLE_NOT_INITIALIZED, pfm->closeFile(handle1), "Close testFile0.db again");
	TEST_FN_EQ( 0, pfm->closeFile(handle2), "Close testFile2.db");
	TEST_FN_EQ( rc::FILE_COULD_NOT_DELETE, pfm->destroyFile("testFile4.db"), "Delete testFile4.db with open handles");
	TEST_FN_EQ( 0, pfm->closeFile(handle3), "Close testFile3.db");
	TEST_FN_EQ( rc::FILE_COULD_NOT_DELETE, pfm->destroyFile("testFile4.db"), "Delete testFile4.db with open handles");
	TEST_FN_EQ( 0, pfm->closeFile(handle4), "Close testFile4.db");
	TEST_FN_EQ( rc::FILE_HANDLE_NOT_INITIALIZED, pfm->closeFile(handle4), "Close testFile4.db through uninitialized file handle");
	TEST_FN_EQ( 0, pfm->destroyFile("testFile4.db"), "Delete testFile4.db with open handles");

	// Test inserting and deleting records
	TEST_FN_EQ( 0, pfm->createFile("testFile4.db"), "Create testFile4.db");
	TEST_FN_EQ( 0, pfm->openFile("testFile4.db", handle4), "Open testFile4.db and store in handle4");
	TEST_FN_EQ( rc::OK, testRandomInsertion(handle4, 20, 1, 40, true), "Testing insertion and deletion of records");
	TEST_FN_EQ( 0, pfm->closeFile(handle4), "Close handle4");

	// Re-open the files to be closed again
	TEST_FN_EQ( 0, pfm->openFile("testFile0.db", handle0), "Open testFile0.db and store in handle0");
	TEST_FN_EQ( 0, pfm->openFile("testFile1.db", handle1), "Open testFile1.db and store in handle1");
	TEST_FN_EQ( 0, pfm->openFile("testFile2.db", handle2), "Open testFile2.db and store in handle2");
	TEST_FN_EQ( 0, pfm->openFile("testFile3.db", handle3), "Open testFile3.db and store in handle3");
	TEST_FN_EQ( 0, pfm->openFile("testFile4.db", handle4), "Open testFile4.db and store in handle4");

	// Clean up
	TEST_FN_EQ( 0, pfm->closeFile(handle0), "Close handle0");
	TEST_FN_EQ( 0, pfm->closeFile(handle1), "Close handle1");
    TEST_FN_EQ( 0, pfm->closeFile(handle2), "Close handle2");
    TEST_FN_EQ( 0, pfm->closeFile(handle3), "Close handle3");
    TEST_FN_EQ( 0, pfm->closeFile(handle4), "Close handle4");
	TEST_FN_EQ( 0, pfm->destroyFile("testFile0.db"), "Destroy testFile0.db");
	TEST_FN_EQ( 0, pfm->destroyFile("testFile1.db"), "Destroy testFile1.db");
    TEST_FN_EQ( 0, pfm->destroyFile("testFile2.db"), "Destroy testFile2.db");
    TEST_FN_EQ( 0, pfm->destroyFile("testFile3.db"), "Destroy testFile3.db");
    TEST_FN_EQ( 0, pfm->destroyFile("testFile4.db"), "Destroy testFile4.db");

	cout << "\nRBFM Tests complete: " << numPassed << "/" << numTests << "\n\n" << endl;
	assert(numPassed == numTests);
}


bool pfmKillProcTest1()
{
	killPFM();
	return PagedFileManager::instance() != NULL;
}

RC pfmKillProcTest2()
{
	RC ret = PagedFileManager::instance()->createFile("testFile1.db");
	if (ret != rc::OK)
	{
		return ret;
	}

	killPFM();

	FileHandle handle1;
	ret = PagedFileManager::instance()->openFile("testFile1.db", handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = PagedFileManager::instance()->closeFile(handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = PagedFileManager::instance()->destroyFile("testFile1.db");
	if (ret != rc::OK)
	{
		return ret;
	}

	return rc::OK;
}

RC pfmKillProcTest3()
{
	RC ret = PagedFileManager::instance()->createFile("testFile1.db");
	if (ret != rc::OK)
	{
		return ret;
	}

	FileHandle handle1;
	ret = PagedFileManager::instance()->openFile("testFile1.db", handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = PagedFileManager::instance()->closeFile(handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	killPFM();

	ret = PagedFileManager::instance()->openFile("testFile1.db", handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = PagedFileManager::instance()->closeFile(handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = PagedFileManager::instance()->destroyFile("testFile1.db");
	if (ret != rc::OK)
	{
		return ret;
	}

	return rc::OK;
}

RC pfmKillProcTest4()
{
	RC ret = PagedFileManager::instance()->createFile("testFile1.db");
	if (ret != rc::OK)
	{
		return ret;
	}

	FileHandle handle1;
	ret = PagedFileManager::instance()->openFile("testFile1.db", handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	char bufferIn[PAGE_SIZE];
	char bufferOut[PAGE_SIZE];
	memset(bufferIn, 0, PAGE_SIZE);
	bufferIn[0] = 1;
	bufferIn[PAGE_SIZE - 1] = 1;

	ret = handle1.appendPage(bufferIn);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = PagedFileManager::instance()->closeFile(handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	killPFM();

	ret = PagedFileManager::instance()->openFile("testFile1.db", handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = handle1.readPage(0, bufferOut);
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = PagedFileManager::instance()->closeFile(handle1);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (memcmp(bufferIn, bufferOut, PAGE_SIZE))
	{
		return -1;
	}

	return rc::OK;
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

	pfm = NULL;
	TEST_FN_EQ( true, pfmKillProcTest1(), "Test that we can successfully destroy and then reinitialize the PFM");
	TEST_FN_EQ( rc::OK, pfmKillProcTest2(), "Test that we can 'kill' the process after createFile");
	TEST_FN_EQ( rc::OK, pfmKillProcTest3(), "Test that we can 'kill' the process after openFile");
	TEST_FN_EQ( rc::OK, pfmKillProcTest4(), "Test that we can 'kill' the process after writing a page");

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
    TEST_FN_EQ(success, pfm->createFile(fileName.c_str()), "Create file");
    TEST_FN_EQ(rc::FILE_ALREADY_EXISTS, pfm->createFile(fileName.c_str()), "Create file that already exists");
    TEST_FN_EQ(true, FileExists(fileName.c_str()), "File exists");

    FileHandle fileHandle;
    TEST_FN_EQ(success, pfm->openFile(fileName.c_str(), fileHandle), "Open file");

    unsigned count = fileHandle.getNumberOfPages();
    TEST_FN_EQ(0, count, "Number of pages correct");

    TEST_FN_EQ(success, pfm->closeFile(fileHandle), "Close file");

    // Re-open, check open again, check pages
    TEST_FN_EQ(success, pfm->openFile(fileName.c_str(), fileHandle), "Open file");
    TEST_FN_EQ(rc::FILE_HANDLE_ALREADY_INITIALIZED, pfm->openFile(fileName.c_str(), fileHandle), "Open file fails when same filehandle already pointing to an open file");

    count = fileHandle.getNumberOfPages();
    TEST_FN_EQ(0, count, "Number of pages correct");

    // Write page 1 (error), write page 0 (error), append page, close
    unsigned char buffer[PAGE_SIZE];
    for (unsigned i = 0; i < PAGE_SIZE; i++) 
    {
        buffer[i] = i % 256;
    }
    TEST_FN_EQ(rc::FILE_PAGE_NOT_FOUND, fileHandle.writePage(1, buffer), "Writing to non-existent page");
    TEST_FN_EQ(rc::FILE_PAGE_NOT_FOUND, fileHandle.writePage(0, buffer), "Writing to non-existent page");
    TEST_FN_EQ(success, fileHandle.appendPage(buffer), "Appending a new page");
    TEST_FN_EQ(success, pfm->closeFile(fileHandle), "Close file");

    // Open, read page 1 (fail), read page 0, check contents
    unsigned char buffer_copy[PAGE_SIZE];
    memset(buffer_copy, 0, PAGE_SIZE);
    TEST_FN_EQ(success, pfm->openFile(fileName.c_str(), fileHandle), "Open file");
    TEST_FN_EQ(rc::FILE_PAGE_NOT_FOUND, fileHandle.readPage(1, buffer_copy), "Reading from non-existent page");
    TEST_FN_EQ(success, fileHandle.readPage(0, buffer_copy), "Reading previously written data");
    TEST_FN_EQ(0, memcmp(buffer, buffer_copy, PAGE_SIZE), "Comparing data read and written");

    // Test appending PAGE_SIZE pages and reading them all (overkill, perhaps)
    for (unsigned i = 0; i < PAGE_SIZE; i++)
    {
        for (unsigned j = 0; j < PAGE_SIZE; j++)
        {
            buffer[j] = buffer[j] ^ 0xFF; // flip for kicks
        }
        rc = fileHandle.appendPage(buffer);
        assert(rc == success);
    }
    for (unsigned i = 1; i < PAGE_SIZE + 1; i++)
    {
        memset(buffer_copy, 0, PAGE_SIZE);
        rc = fileHandle.readPage(i, buffer_copy);
        assert(rc == success);
        for (unsigned j = 0; j < PAGE_SIZE; j++)
        {
            if (i % 2 == 1) assert((buffer_copy[j] ^ 0xFF) == (j % 256)); // flip for kicks
            else assert(buffer_copy[j] == (j % 256)); // flip for kicks
        }
    }

    TEST_FN_EQ(success, pfm->closeFile(fileHandle), "Close file");

    // Test multiple file handles to open/close/delete correctness
    TEST_FN_EQ(success, pfm->openFile(fileName.c_str(), fileHandle), "Open file");

    FileHandle fileHandle2;
    TEST_FN_EQ(success, pfm->openFile(fileName.c_str(), fileHandle2), "Open second handle to same file");

    memset(buffer, 0, PAGE_SIZE);
    memset(buffer_copy, 0, PAGE_SIZE);
    rc = fileHandle.readPage(1, buffer);
    assert(rc == success);
    rc = fileHandle2.readPage(1, buffer_copy);
    assert(rc == success);
    TEST_FN_EQ(0, memcmp(buffer, buffer_copy, PAGE_SIZE), "Reading same file through two different handles");

    // Test close/delete correctness
    TEST_FN_EQ(rc::FILE_COULD_NOT_DELETE, pfm->destroyFile(fileName.c_str()), "Destroy attempt #1 when file is still open (two pins)");
    rc = pfm->closeFile(fileHandle2);
    assert(rc == success);
    TEST_FN_EQ(rc::FILE_COULD_NOT_DELETE, pfm->destroyFile(fileName.c_str()), "Destroy attempt #2 when file is still open (one pin)");    
    TEST_FN_EQ(rc::FILE_HANDLE_NOT_INITIALIZED, pfm->closeFile(fileHandle2), "Multiple close through the same handle");
    rc = pfm->closeFile(fileHandle);
    assert(rc == success);
    TEST_FN_EQ(success, pfm->destroyFile(fileName.c_str()), "Destroy attempt with no pins");    

    // Test deletion of non-existent file
    string dummy = "dummy"; 
    TEST_FN_EQ(rc::FILE_COULD_NOT_DELETE, pfm->destroyFile(dummy.c_str()), "Destroy non-existent file");

    cout << "\nFM Tests complete: " << numPassed << "/" << numTests << "\n\n" << endl;
    assert(numPassed == numTests);
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
    printf("%d\n", count);
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
    string fileName = "test_5";

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
    char record[1000];
    char record_copy[1000];
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

    cout << "Test Case 11 Passed!" << endl << endl;

    return 0;
}

RC rbfmTestReadAttribute(RecordBasedFileManager *rbfm, vector<RID> &rids, vector<int> &sizes)
{
	RC rc = rc::OK;
    string fileName = "rbfmTestReadAttribute_file";

    // Create a file named "rbfmTestReadAttribute_file"
    rc = rbfm->createFile(fileName.c_str());
    assert(rc == success);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case rbfmTestReadAttribute Failed!" << endl << endl;
        return -1;
    }

    // Open the file "rbfmTestReadAttribute_file"
    FileHandle fileHandle;
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    RID rid; 
    char record[1000];
    char record_copy[1000];
    int numRecords = 10;

    vector<Attribute> recordDescriptor;
    createLargeRecordDescriptor(recordDescriptor);

    // Insert 'numRecords' records into file
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

        // Walk each attribute and extract/compare the expected value
        for (int j = 0; j < numRecords * 3; j += 3) // each tuple has three attributes, and we're only getting the strings here
        {
        	const char* tempBuffer = (char*)malloc(i + 1);
        	stringstream out;
        	out << j;
        	string attrName = "attr" + out.str();
			rbfm->readAttribute(fileHandle, recordDescriptor, rid, attrName, (void*)tempBuffer);

        	// Rebuild the expected string
        	char text = i % 26 + 97;
        	int count = i % 50 + 1;
        	char* charBuf = (char*)malloc(count);
        	memcpy((char*)charBuf, &count, sizeof(int));
        	for (int k = 4; k < count + 4; k++)
        	{
        		memcpy((char*)charBuf + k, &text, 1);
        	}
        	assert(strncmp(tempBuffer, charBuf, count) == 0);
        }

        // Walk the ints & floats now and compare equality
        for (int j = 0; j < numRecords * 3; j += 3)
        {
        	unsigned intVal = 0;
        	float floatVal = 0.0;
        	stringstream out1;
        	out1 << (j+1);
        	string attrName = "attr" + out1.str();
        	rbfm->readAttribute(fileHandle, recordDescriptor, rid, attrName, &intVal);
        	cout << "read (" << (j+1) << "): " << intVal << endl;
        	assert(intVal == i);

        	stringstream out2;
        	out2 << (j+2);
        	attrName = "attr" + out2.str();
        	rbfm->readAttribute(fileHandle, recordDescriptor, rid, attrName, &floatVal);
        	cout << "read (" << (j+2) << "): " << floatVal << endl;
        	assert(floatVal == (i+1));
        }

        rids.push_back(rid);
        sizes.push_back(size);        
    }
    // Close the file "test_4"
    rc = rbfm->closeFile(fileHandle);
    assert(rc == success);

	return rc;
}

RC rbfmTestReorganizePage(RecordBasedFileManager *rbfm, int skip)
{
	RC rc = rc::OK;
    string fileName = "rbfmTestReorganizePage_file";

    vector<RID> rids;
    vector<int> sizes;

    // Create a file named "our_test_4"
    rc = rbfm->createFile(fileName.c_str());
    assert(rc == rc::OK);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case Failed!" << endl << endl;
        return -1;
    }

    // Open the file "our_test_4"
    FileHandle fileHandle;
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    RID rid; 
    char record[1000];
    char record_copy[1000];
    int numRecords = 50;

    vector<Attribute> recordDescriptor;
    createLargeRecordDescriptor(recordDescriptor);

    // Store references to records inserted so that they can be easily checked later
    vector<char*> ridContentList;

    // Insert some records into the file
    for(int i = 0; i < numRecords; i++)
    {
        // Test insert Record
        int size = 0;
        memset(record, 0, 1000);
        memset(record_copy, 0, 1000);
        prepareLargeRecord(i, record, &size);

        // Allocate space on the content list
        ridContentList.push_back((char*)malloc(1000));

        // Write, read, and then immediately compare the two
        rc = rbfm->insertRecord(fileHandle, recordDescriptor, record, rid);
        assert(rc == success);
        rc = rbfm->readRecord(fileHandle, recordDescriptor, rid, record_copy);
        assert(rc == success);
        assert(memcmp(record, record_copy, size) == 0);
        memcpy(ridContentList[i], record, size);

        // Record the RIDs
        rids.push_back(rid);
        sizes.push_back(size);        
    }

    // Create and populate a vector of vectors that maps page numbers to all RIDs on that page
    vector< vector<RID> > pageRIDs;
    vector< vector<int> > pageRIDIndexes;
    for (PageNum page = 0; page < fileHandle.getNumberOfPages(); page++)
    {
    	vector<RID> tmp;
    	pageRIDs.push_back(tmp);
    	vector<int> tmpIndexes;
    	pageRIDIndexes.push_back(tmpIndexes);
    }
    for (unsigned int i = 0; i < rids.size(); i++)
    {
    	RID rid = rids[i];
    	pageRIDs[rid.pageNum].push_back(rid);
    	pageRIDIndexes[rid.pageNum].push_back(i);
    }

    // Skip across and delete some records
    for (int i = 0; i < numRecords; i += skip)
    {
    	rbfm->deleteRecord(fileHandle, recordDescriptor, rids[i]);
    }

    // Double-check deletion
    for (unsigned int pageNum = 0; pageNum < pageRIDs.size(); pageNum++)
    {
    	for (unsigned int pageRidIndex = 0; pageRidIndex < pageRIDs[pageNum].size(); pageRidIndex++)
    	{
    		int ridIndex = (pageRIDIndexes[pageNum])[pageRidIndex];
    		if (ridIndex % skip != 0)
    		{
    			memset(record, 0, 1000);
				rc = rbfm->readRecord(fileHandle, recordDescriptor, (pageRIDs[pageNum])[pageRidIndex], record);
				assert(rc == success);
		    	assert(memcmp(record, ridContentList[ridIndex], sizes[ridIndex]) == 0);
    		}
    	}
    }

    // Walk every page and do some reallocation
    for (PageNum page = 1; page < fileHandle.getNumberOfPages(); page++)
    {
    	rc = rbfm->reorganizePage(fileHandle, recordDescriptor, page);
    	assert(rc == rc::OK || rc == rc::PAGE_CANNOT_BE_ORGANIZED);
    }

    // Now check the contents again
    for (unsigned int pageNum = 0; pageNum < pageRIDs.size(); pageNum++)
    {
    	for (unsigned int pageRidIndex = 0; pageRidIndex < pageRIDs[pageNum].size(); pageRidIndex++)
    	{
    		int ridIndex = (pageRIDIndexes[pageNum])[pageRidIndex];
    		if (ridIndex % skip != 0)
    		{
    			memset(record, 0, 1000);
				rc = rbfm->readRecord(fileHandle, recordDescriptor, (pageRIDs[pageNum])[pageRidIndex], record);
				assert(rc == success);
		    	assert(memcmp(record, ridContentList[ridIndex], sizes[ridIndex]) == 0);
    		}
    	}
    }

    // Close & delete the file "test_4"
    rc = rbfm->closeFile(fileHandle);
    assert(rc == success);
    rc = rbfm->destroyFile(fileName.c_str());
    assert(rc == success);

	return rc;
}

RC rbfmTestUdpateRecord(RecordBasedFileManager *rbfm, int skip)
{
	RC rc = rc::OK;
    string fileName = "rbfmTestUdpateRecord_file";

    vector<RID> rids;
    vector<int> sizes;

    // Create a file named "our_test_4"
    rc = rbfm->createFile(fileName.c_str());
    assert(rc == rc::OK);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case Failed!" << endl << endl;
        return -1;
    }

    // Open the file "our_test_4"
    FileHandle fileHandle;
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    RID rid; 
    char record[(PAGE_SIZE / 2) + sizeof(unsigned)];
    char record_copy[(PAGE_SIZE / 2) + sizeof(unsigned)];
    int numRecords = 50;
    unsigned halfPageSize = PAGE_SIZE / 2;
    unsigned quarterPageSize = PAGE_SIZE / 4;
    unsigned eighthPageSize = PAGE_SIZE / 8;

    // Create the record descriptor for a single, long string
    vector<Attribute> recordDescriptor;
    Attribute attr;
    attr.name = "attr";
    attr.type = TypeVarChar;
    attr.length = (AttrLength)PAGE_SIZE;
    recordDescriptor.push_back(attr);

    // Store references to records inserted so that they can be easily checked later
    vector<char*> ridContentList;

    // Insert some records into the file
    for(int i = 0; i < numRecords; i++)
    {
        // Test insert Record
        memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
        memset(record_copy, 0, (PAGE_SIZE / 2) + sizeof(unsigned));

        memcpy((char*)record, &quarterPageSize, sizeof(unsigned));
        unsigned offset = sizeof(unsigned);
        for(int j = 0; j < quarterPageSize; j++)
        {
            memcpy((char *)record + offset, "a", 1);
            offset += 1;
        }

        // Allocate space on the content list
        ridContentList.push_back((char*)malloc(offset));

        // Write, read, and then immediately compare the two
        rc = rbfm->insertRecord(fileHandle, recordDescriptor, record, rid);
        assert(rc == success);
        rc = rbfm->readRecord(fileHandle, recordDescriptor, rid, record_copy);
        assert(rc == success);
        assert(memcmp(record, record_copy, offset) == 0);
        memcpy(ridContentList[i], record, offset);

        // Record the RIDs
        rids.push_back(rid);
        sizes.push_back(offset);
    }

    // Expand every other "skipped" record
    for (int i = 0; i < numRecords; i += skip)
    {
    	memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
	    memcpy((char*)record, &halfPageSize, sizeof(unsigned));
	    unsigned offset = sizeof(unsigned);
	    for(int j = 0; j < halfPageSize; j++)
	    {
	        memcpy((char *)record + offset, "a", 1);
	        offset += 1;
	    }

	    // Update the record
	    rc = rbfm->updateRecord(fileHandle, recordDescriptor, record, rids[i]);
	    assert(rc == success);

	    // Read it back in and compare
	    memset(record_copy, 0, PAGE_SIZE / 2);
	    rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record_copy);
	    rbfm->printRecord(recordDescriptor, record);
	    cout << endl;
	    rbfm->printRecord(recordDescriptor, record_copy);
	    assert(rc == success);
	    assert(memcmp(record, record_copy, offset) == 0);
    }

    // Shrink every other "skipped" record
    for (int i = 0; i < numRecords; i += skip)
    {
    	memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
	    memcpy((char*)record, &eighthPageSize, sizeof(unsigned));
	    unsigned offset = sizeof(unsigned);
	    for(int j = 0; j < eighthPageSize; j++)
	    {
	        memcpy((char *)record + offset, "a", 1);
	        offset += 1;
	    }

	    // Update the record
	    rc = rbfm->updateRecord(fileHandle, recordDescriptor, record, rids[i]);
	    assert(rc == success);

	    // Read it back in and compare
	    memset(record_copy, 0, PAGE_SIZE / 2);
	    rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record_copy);
	    assert(rc == success);
	    assert(memcmp(record, record_copy, offset) == 0);
    }

    return rc::OK;
}

RC rbfmTestReorganizeDeleteUpdate(RecordBasedFileManager *rbfm, int skip)
{
	RC rc = rc::OK;
    string fileName = "rbfmTestReorganizeDeleteUpdate_file";

    vector<RID> rids;
    vector<int> sizes;

    // Create a file named "our_test_4"
    rc = rbfm->createFile(fileName.c_str());
    assert(rc == rc::OK);

    if(FileExists(fileName.c_str()))
    {
        cout << "File " << fileName << " has been created." << endl;
    }
    else
    {
        cout << "Failed to create file!" << endl;
        cout << "Test Case Failed!" << endl << endl;
        return -1;
    }

    // Open the file "our_test_4"
    FileHandle fileHandle;
    rc = rbfm->openFile(fileName.c_str(), fileHandle);
    assert(rc == success);

    RID rid; 
    char record[(PAGE_SIZE / 2) + sizeof(unsigned)];
    char record_copy[(PAGE_SIZE / 2) + sizeof(unsigned)];
    int numRecords = 10;
    unsigned halfPageSize = PAGE_SIZE / 2;
    unsigned quarterPageSize = PAGE_SIZE / 4;
    unsigned eighthPageSize = PAGE_SIZE / 8;

    // Create the record descriptor for a single, long string
    vector<Attribute> recordDescriptor;
    Attribute attr;
    attr.name = "attr";
    attr.type = TypeVarChar;
    attr.length = (AttrLength)PAGE_SIZE;
    recordDescriptor.push_back(attr);

    // Store references to records inserted so that they can be easily checked later
    vector<char*> ridContentList;

    // Insert some records into the file
    for(int i = 0; i < numRecords; i++)
    {
        // Test insert Record
        memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
        memset(record_copy, 0, (PAGE_SIZE / 2) + sizeof(unsigned));

        memcpy((char*)record, &quarterPageSize, sizeof(unsigned));
        unsigned offset = sizeof(unsigned);
        for(int j = 0; j < quarterPageSize; j++)
        {
            memcpy((char *)record + offset, "a", 1);
            offset += 1;
        }

        // Allocate space on the content list
        ridContentList.push_back((char*)malloc(offset));

        // Write, read, and then immediately compare the two
        rc = rbfm->insertRecord(fileHandle, recordDescriptor, record, rid);
        assert(rc == success);
        rc = rbfm->readRecord(fileHandle, recordDescriptor, rid, record_copy);
        assert(rc == success);
        assert(memcmp(record, record_copy, offset) == 0);
        memcpy(ridContentList[i], record, offset);

        // Record the RIDs
        rids.push_back(rid);
        sizes.push_back(offset);
    }

    // Create and populate a vector of vectors that maps page numbers to all RIDs on that page
    vector< vector<RID> > pageRIDs;
    vector< vector<int> > pageRIDIndexes;
    for (PageNum page = 0; page < fileHandle.getNumberOfPages(); page++)
    {
    	vector<RID> tmp;
    	pageRIDs.push_back(tmp);
    	vector<int> tmpIndexes;
    	pageRIDIndexes.push_back(tmpIndexes);
    }
    for (unsigned int i = 0; i < rids.size(); i++)
    {
    	RID rid = rids[i];
    	pageRIDs[rid.pageNum].push_back(rid);
    	pageRIDIndexes[rid.pageNum].push_back(i);
    }

    // Skip across and delete some records
    for (int i = 0; i < numRecords; i += skip)
    {
    	cout << "Deleting " << rids[i].pageNum << "," << rids[i].slotNum << endl;
    	rc = rbfm->deleteRecord(fileHandle, recordDescriptor, rids[i]);
    	assert(rc == success);

    	memset(record, 0, 1000);
    	rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record);
    	assert(rc == rc::RECORD_DELETED);
    }

    // Double-check contents
    for (unsigned int pageNum = 0; pageNum < pageRIDs.size(); pageNum++)
    {
    	for (unsigned int pageRidIndex = 0; pageRidIndex < pageRIDs[pageNum].size(); pageRidIndex++)
    	{
    		int ridIndex = (pageRIDIndexes[pageNum])[pageRidIndex];
    		if (ridIndex % skip != 0)
    		{
    			memset(record, 0, 1000);
				rc = rbfm->readRecord(fileHandle, recordDescriptor, (pageRIDs[pageNum])[pageRidIndex], record);
				assert(rc == success);
		    	assert(memcmp(record, ridContentList[ridIndex], sizes[ridIndex]) == 0);
    		}
    	}
    }

    // Walk every page and do some reallocation
    for (PageNum page = 1; page < fileHandle.getNumberOfPages(); page++)
    {
    	rc = rbfm->reorganizePage(fileHandle, recordDescriptor, page);
    	assert(rc == rc::OK || rc == rc::PAGE_CANNOT_BE_ORGANIZED);
    }

    // Double-check contents
    for (int i = 0; i < numRecords; i++)
    {
		memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
		rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record);
		if (rc != rc::RECORD_DELETED)
		{
			assert(rc == success);
			assert(memcmp(record, ridContentList[i], sizes[i]) == 0);
		}
    }

    // Expand every other "skipped" record
    for (int i = 1; i < numRecords; i += skip)
    {
    	memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
	    memcpy((char*)record, &halfPageSize, sizeof(unsigned));
	    unsigned offset = sizeof(unsigned);
	    for(int j = 0; j < halfPageSize; j++)
	    {
	        memcpy((char *)record + offset, "a", 1);
	        offset += 1;
	    }

	    cout << "Expanding " << rids[i].pageNum << "," << rids[i].slotNum << endl;
	    free(ridContentList[i]);
	    ridContentList[i] = (char*)malloc((PAGE_SIZE / 2) + sizeof(unsigned));
		memcpy(ridContentList[i], record, offset);
		sizes[i] = offset;

	    // Update the record
	    rc = rbfm->updateRecord(fileHandle, recordDescriptor, record, rids[i]);
	    assert(rc == success);

	    // Read it back in and compare
	    memset(record_copy, 0, PAGE_SIZE / 2);
	    rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record_copy);
	    assert(rc == success);
	    assert(memcmp(record, record_copy, offset) == 0);
	    assert(memcmp(ridContentList[i], record_copy, offset) == 0);
    }

    // Double-check contents
    for (int i = 0; i < numRecords; i++)
    {
		memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
		rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record);
		if (rc != rc::RECORD_DELETED)
		{
			assert(rc == success);
			assert(strncmp(record, ridContentList[i], sizes[i]) == 0);
		}
    }

    // Skip across and delete some more records
    for (int i = numRecords / 4; i < numRecords; i += skip)
    {
    	cout << "Deleting " << rids[i].pageNum << "," << rids[i].slotNum << endl;
    	rbfm->deleteRecord(fileHandle, recordDescriptor, rids[i]);
    }

    // Walk every page and do some reallocation
    for (PageNum page = 1; page < fileHandle.getNumberOfPages(); page++)
    {
    	rc = rbfm->reorganizePage(fileHandle, recordDescriptor, page);
    	assert(rc == rc::OK || rc == rc::PAGE_CANNOT_BE_ORGANIZED);
    }

    // Shrink every other "skipped" record
    for (int i = 2; i < numRecords; i += skip)
    {
    	memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
	    memcpy((char*)record, &eighthPageSize, sizeof(unsigned));
	    unsigned offset = sizeof(unsigned);
	    for(int j = 0; j < eighthPageSize; j++)
	    {
	        memcpy((char *)record + offset, "a", 1);
	        offset += 1;
	    }

	    free(ridContentList[i]);
	    ridContentList[i] = (char*)malloc(offset);
		memcpy(ridContentList[i], record, offset);
		sizes[i] = offset;

	    // Update the record
	    rc = rbfm->updateRecord(fileHandle, recordDescriptor, record, rids[i]);
	    if (rc != rc::RECORD_DELETED)
	    {
	    	assert(rc == success);

		    // Read it back in and compare
		    memset(record_copy, 0, PAGE_SIZE / 2);
		    rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record_copy);
		    assert(rc == success);
		    assert(memcmp(record, record_copy, offset) == 0);
	    }
    }

    // Double-check contents
    for (int i = 0; i < numRecords; i++)
    {
		memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
		rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record);
		if (rc != rc::RECORD_DELETED && rc != rc::RECORD_IS_ANCHOR)
		{
			assert(rc == success);
			assert(memcmp(record, ridContentList[i], sizes[i]) == 0);
		}
    }

    // Skip across and delete some more records
    for (int i = numRecords / 2; i < numRecords; i += skip)
    {
    	rbfm->deleteRecord(fileHandle, recordDescriptor, rids[i]);
    }

    // Walk every page and do some reallocation
    for (PageNum page = 1; page < fileHandle.getNumberOfPages(); page++)
    {
    	rc = rbfm->reorganizePage(fileHandle, recordDescriptor, page);
    	assert(rc == rc::OK || rc == rc::PAGE_CANNOT_BE_ORGANIZED);
    }

	// Double-check contents
    for (int i = 0; i < numRecords; i++)
    {
		memset(record, 0, (PAGE_SIZE / 2) + sizeof(unsigned));
		rc = rbfm->readRecord(fileHandle, recordDescriptor, rids[i], record);
		if (rc != rc::RECORD_DELETED && rc != rc::RECORD_IS_ANCHOR)
		{
			assert(rc == success);
			assert(memcmp(record, ridContentList[i], sizes[i]) == 0);
		}
    }

    // Close & delete the file "test_4"
    rc = rbfm->closeFile(fileHandle);
    assert(rc == success);
    rc = rbfm->destroyFile(fileName.c_str());
    assert(rc == success);

	return rc;
}

void rbfmTestRandomInsertDeleteUpdateReorganize(int numRecords)
{
	RecordBasedFileManager *rbfm = RecordBasedFileManager::instance();

	RC rc = rbfm->createFile("rbfmTestRandomInsertDeleteUpdateReorganize_file");
	assert(rc == success);

	FileHandle file;
	rc = rbfm->openFile("rbfmTestRandomInsertDeleteUpdateReorganize_file", file);
	assert(rc == success);

	char readData[PAGE_SIZE] = {0};
	vector<char*> recordData;
	vector<int> stringSizes;
	vector<int> indexes;
	vector<RID> rids;

	// Allocate enough space for any sized record
	for (int i=0; i<numRecords; ++i)
	{
		rids.push_back(RID());
		indexes.push_back(i);
		recordData.push_back((char*)malloc(PAGE_SIZE));
		memset(recordData.back(), 0, PAGE_SIZE);
	}

	// Determine the size of the records
	for (int i=0; i<numRecords; ++i)
	{
		int stringsize = 4 + (rand() % 4000);
		stringSizes.push_back(stringsize);

		int* pss = (int*)recordData[i];
		*pss = stringsize;

		// Fill out the data with random data
		char* strdata = recordData[i] + 4;
		for (int c=0; c<stringsize; ++c)
		{
			strdata[c] = 'a' + (rand()%50);
		}
	}

	// Setup our simple record description
	Attribute attr;
	attr.length = 4000;
	attr.name = "string";
	attr.type = TypeVarChar;

	vector<Attribute> recordDescriptor;
	recordDescriptor.push_back(attr);

	// Start adding in records
	std::random_shuffle(indexes.begin(), indexes.end());
	for (int i=0; i<numRecords; ++i)
	{
		int index = indexes[i];
		rbfm->insertRecord(file, recordDescriptor, recordData[index], rids[index]);
	}

	// Check records are correct still
	for (int i=0; i<numRecords; ++i)
	{
		rbfm->readRecord(file, recordDescriptor, rids[i], readData);
		memcmp(recordData[i], readData, 4 + stringSizes[i]);
	}

	// reduce the size of half of the records
	std::random_shuffle(indexes.begin(), indexes.end());
	for (int i=0; i<numRecords/2; ++i)
	{
		int index = indexes[i];
		std::cout << "decreasing size of " << index;

		char* data = recordData[index];
		int* strsize = (int*)data;
		assert(*strsize == stringSizes[index]);


		std::cout << " oldsize=" << (*strsize) << " ";
		if (stringSizes[index] > 1000)
		{
			stringSizes[index] /= (1 + (rand() % 31));
		}
		else if (stringSizes[index] > 100)
		{
			stringSizes[index] -= (1 + (rand() % 57));
		}
		else if (stringSizes[index] > 10)
		{
			stringSizes[index] -= 4;
		}

		*strsize = stringSizes[index];
		std::cout << " newsize=" << (*strsize) << " ";
		std::cout << std::endl;

		// change the data again so we can recompare correctly
		char* strdata = data + 4;
		for (int c=0; c<stringSizes[index]; ++c)
		{
			strdata[c] = 'a' + (rand()%50);
		}

		// update record on disk
		rbfm->updateRecord(file, recordDescriptor, data, rids[index]);
	}

	// Check records are correct still
	for (int i=0; i<numRecords; ++i)
	{
		rbfm->readRecord(file, recordDescriptor, rids[i], readData);
		memcmp(recordData[i], readData, 4 + stringSizes[i]);
	}

	// increase the size of half of the records
	std::random_shuffle(indexes.begin(), indexes.end());
	for (int i=0; i<numRecords/2; ++i)
	{
		int index = indexes[i];
		std::cout << "increasing size of " << index << "\n";

		char* data = recordData[index];
		int* strsize = (int*)data;
		assert(*strsize == stringSizes[index]);

		std::cout << " oldsize=" << (*strsize) << " ";

		stringSizes[index] += (rand()%(4000 - stringSizes[index]));
		if (stringSizes[index] > 4000)
		{
			stringSizes[index] = 4001;
		}

		*strsize = stringSizes[index];
		std::cout << " newsize=" << (*strsize) << " ";
		std::cout << std::endl;

		// change the data again so we can recompare correctly
		char* strdata = data + 4;
		for (int c=0; c<stringSizes[index]; ++c)
		{
			strdata[c] = 'a' + (rand()%50);
		}

		// update record on disk
		rbfm->updateRecord(file, recordDescriptor, data, rids[index]);
	}

	// Check records are correct still
	for (int i=0; i<numRecords; ++i)
	{
		rbfm->readRecord(file, recordDescriptor, rids[i], readData);
		memcmp(recordData[i], readData, 4 + stringSizes[i]);
	}

	rbfm->closeFile(file);
}

void cleanup()
{
	remove("test");
    remove("test_1");
    remove("test_2");
    remove("test_3");
    remove("test_4");
    remove("test_5");
    remove("testFile0.db");
    remove("testFile1.db");
    remove("testFile2.db");
    remove("testFile3.db");
    remove("testFile4.db");
    remove("testFile5.db");
    remove("fh_test");
    remove("rbfmTestReadAttribute_file");
    remove("rbfmTestReorganizePage_file");
    remove("rbfmTestUdpateRecord_file");
    remove("rbfmTestReorganizeDeleteUpdate_file");
	remove("rbfmTestRandomInsertDeleteUpdateReorganize_file");
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

    // Our tests
    RBFTest_11(rbfm, rids, sizes);
    rbfmTestReadAttribute(rbfm, rids, sizes);
	// rbfmTestRandomInsertDeleteUpdateReorganize(49);
	
    // Test the update record by varying how many records are upgraded each time
    for (int i = UPDATE_MIN_SKIP; i <= UPDATE_MAX_SKIP; i++)
    {
    	cleanup();
    	rbfmTestUdpateRecord(rbfm, i);
    }

    // Test the reorganizePage method with varying gaps on each page
	for (int i = REORGANIZE_MIN_SKIP; i <= REORGANIZE_MAX_SKIP; i++)
    {
    	cleanup();
    	rbfmTestReorganizePage(rbfm, i);
    }

    for (int i = ALL_MIN_SKIP; i <= ALL_MAX_SKIP; i++)
    {
    	cleanup();
    	// rbfmTestReorganizeDeleteUpdate(rbfm, i);
    }

    // Finishing up
	pfmTest();
    fhTest();
	rbfmTest();

    cleanup();

    // Clean up remaining memory so it doesn't look like we leaked to valgrind
    delete reinterpret_cast<DeletablePFM*>(PagedFileManager::instance());
    delete reinterpret_cast<DeletableRBFM*>(RecordBasedFileManager::instance());
    return 0;
}
