
#ifndef _rbfm_h_
#define _rbfm_h_

#include <string>
#include <vector>
#include <stdint.h>

#include "pfm.h"
#include "../util/dbgout.h"

using namespace std;

#define CURRENT_PF_VERSION 1
#define NUM_FREESPACE_LISTS 11

// Record ID
// Uniquely identifies the location in the file where the record is stored
typedef struct
{
  PageNum pageNum;
  unsigned slotNum;
} RID;

// Page index slot entry
// Data required to find, and copy the exact amount of size required for a record
typedef struct
{
  unsigned size; // size is for the entire record, and includes the header information with the offsets and the length of each resp. field
  unsigned pageOffset;
} PageIndexSlot;

// Page index (directory)
/*
Data required for keeping track of how much room is available on this page, along with basic bookkeeping information
The slotNum of an RID will (backwards) index into the list of PageIndexSlots, thus retrieving the size and location of the record
/-----------------------------------------\
| Page N                                  |
| --------------------------------------- |
| [Rec size][Rec offsets][Rec data......] |
| [Rec size][Rec offsets....][Rec data... |
| ............[Rec Size][Rec offsets][Rec |
| data.......]                            |
|               <free space>              |
|                                         |
| [PageIndexSlot_K] [PageIndexSlot_K-1]   |
| ... [PageIndexSlot_0] [PageIndexHeader] |
\-----------------------------------------/
*/

struct PageIndexHeader
{
  unsigned pageNumber;
  unsigned freeSpaceOffset;
  unsigned numSlots;

  // Doubly linked list data for whichever freespace list we are attached to
  unsigned freespaceList;
  PageNum prevPage;
  PageNum nextPage;
};


// Attribute
typedef enum { TypeInt = 0, TypeReal, TypeVarChar } AttrType;
typedef unsigned AttrLength;
struct Attribute {
    string   name;     // attribute name
    AttrType type;     // attribute type
    AttrLength length; // attribute length
};

// Comparison Operator (NOT needed for part 1 of the project)
typedef enum { EQ_OP = 0,  // =
           LT_OP,      // <
           GT_OP,      // >
           LE_OP,      // <=
           GE_OP,      // >=
           NE_OP,      // !=
           NO_OP       // no condition
} CompOp;

// Page FreeSpace list data
// The head of a freespace list
struct FreeSpaceList
{
    unsigned short cutoff; // Any pages on this list are garunteed to have >= cutoff free bytes
    PageNum listHead; // Point to the first page in this list, 0 if list is empty
};

// PageFile Header (must fit on page #0)
// The file header, data which we store on page 0 that allows us to access free pages
struct PFHeader
{
    PFHeader();
    void init();
    RC validate();

    // Verification that we are using the correct version of the file with constants that are known
    unsigned headerSize;
    unsigned pageSize;
    unsigned version;
    unsigned numPages;
    unsigned numFreespaceLists;

    // Lists of pages with free space
    FreeSpaceList freespaceLists[NUM_FREESPACE_LISTS];
};


/****************************************************************************
The scan iterator is NOT required to be implemented for part 1 of the project 
*****************************************************************************/

# define RBFM_EOF (-1)  // end of a scan operator

// RBFM_ScanIterator is an iteratr to go through records
// The way to use it is like the following:
//  RBFM_ScanIterator rbfmScanIterator;
//  rbfm.open(..., rbfmScanIterator);
//  while (rbfmScanIterator.getNextRecord(rid, data) != RBFM_EOF) {
//    process the data;
//  }
//  rbfmScanIterator.close();


class RBFM_ScanIterator {
public:
  RBFM_ScanIterator() {}
  ~RBFM_ScanIterator() {}

  // "data" follows the same format as RecordBasedFileManager::insertRecord()
  RC getNextRecord(RID & /*rid*/, void * /*data*/) { return RBFM_EOF; }
  RC close() { return -1; }
};


class RecordBasedFileManager
{
public:
  static RecordBasedFileManager* instance();

  RC createFile(const string &fileName);
  
  RC destroyFile(const string &fileName);
  
  RC openFile(const string &fileName, FileHandle &fileHandle);
  
  RC closeFile(FileHandle &fileHandle);

  //  Format of the data passed into the function is the following:
  //  1) data is a concatenation of values of the attributes
  //  2) For int and real: use 4 bytes to store the value;
  //     For varchar: use 4 bytes to store the length of characters, then store the actual characters.
  //  !!!The same format is used for updateRecord(), the returned data of readRecord(), and readAttribute()
  RC insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid);
  RC readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data);
  
  // This method will be mainly used for debugging/testing
  RC printRecord(const vector<Attribute> &recordDescriptor, const void *data);

  RC deleteRecords(FileHandle &fileHandle);

  RC deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid);

  // Assume the rid does not change after update
  RC updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid);

  RC readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data);

  RC reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber);

  // scan returns an iterator to allow the caller to go through the results one by one. 
  RC scan(FileHandle &fileHandle,
      const vector<Attribute> &recordDescriptor,
      const string &conditionAttribute,
      const CompOp compOp,                  // comparision type such as "<" and "="
      const void *value,                    // used in the comparison
      const vector<string> &attributeNames, // a list of projected attributes
      RBFM_ScanIterator &rbfm_ScanIterator);


// Extra credit for part 2 of the project, please ignore for part 1 of the project
public:

  RC reorganizeFile(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor);


protected:
  RecordBasedFileManager();
  ~RecordBasedFileManager();

  RC findFreeSpace(FileHandle &fileHandle, unsigned bytes, PageNum& pageNum);
  RC writeHeader(FileHandle &fileHandle, PFHeader* header);
  RC readHeader(FileHandle &fileHandle, PFHeader* header);
  RC movePageToFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader, unsigned destinationListIndex);
  RC movePageToCorrectFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader);

  static PageIndexSlot* getPageIndexSlot(void* pageBuffer, const RID& rid);

private:
  static RecordBasedFileManager *_rbf_manager;
  PagedFileManager& _pfm;
};


#endif
