
#ifndef _rbfm_h_
#define _rbfm_h_

#include <string>
#include <vector>

#include "../rbf/pfm.h"

#include <boost/unordered_map.hpp>

using namespace std;

#define CURRENT_PF_VERSION 1
#define NUM_FREESPACE_LISTS 11

// Record ID
typedef struct
{
  PageNum pageNum;
  unsigned slotNum;
} RID;

// Struct for a particular record
typedef struct 
{
  unsigned numFields;
  void* fields;
} Record;

// Page index slot entry
typedef struct
{
  unsigned size; // size is for the entire record, and includes the header information with the offsets and the length of each resp. field
  unsigned pageOffset;
} PageIndexSlot;

// Page index (directory)
typedef struct
{
  unsigned pageNumber;
  unsigned freeSpaceOffset;
  unsigned numSlots;

  // Freespace list data
  unsigned freespaceList;
  PageNum prevPage;
  PageNum nextPage;
} PageIndexHeader;


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


// PageFile Header (should fit in 1st page)
typedef struct 
{
  unsigned headerSize;
  unsigned pageSize;
  unsigned version;
  unsigned numPages;
  unsigned numFreespaceLists;

  unsigned short freespaceCutoffs[NUM_FREESPACE_LISTS];
  PageNum freespaceLists[NUM_FREESPACE_LISTS];
} PFHeader;


/****************************************************************************
The scan iterator is NOT required to be implemented for part 1 of the project 
*****************************************************************************/

# define RBFM_EOF (-1)  // end of a scan operator

// RBFM_ScanIterator is an iteratr to go through records
// The way to use it is like the following:
//  RBFM_ScanIterator rbfmScanIterator;
//  rbfm.open(..., rbfmScanIterator);
//  while (rbfmScanIterator(rid, data) != RBFM_EOF) {
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

//// OUR ADDED METHODs
  RC validate(PFHeader &header);
  void init(PFHeader &header);

  //  Format of the data passed into the function is the following:
  //  1) data is a concatenation of values of the attributes
  //  2) For int and real: use 4 bytes to store the value;
  //     For varchar: use 4 bytes to store the length of characters, then store the actual characters.
  //  !!!The same format is used for updateRecord(), the returned data of readRecord(), and readAttribute()
  RC insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid);
  RC readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data);
  
  // This method will be mainly used for debugging/testing
  RC printRecord(const vector<Attribute> &recordDescriptor, const void *data);

/**************************************************************************************************************************************************************
***************************************************************************************************************************************************************
IMPORTANT, PLEASE READ: All methods below this comment (other than the constructor and destructor) are NOT required to be implemented for part 1 of the project
***************************************************************************************************************************************************************
***************************************************************************************************************************************************************/
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
  RC movePageToFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader, unsigned destinationListIndex);
  RC movePageToCorrectFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader);

private:
  static RecordBasedFileManager *_rbf_manager;

  boost::unordered_map<FileHandle*, PFHeader*> _headerData;
  PagedFileManager& _pfm;
};


#endif
