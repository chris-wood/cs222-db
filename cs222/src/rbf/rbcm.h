#ifndef _rbcm_h_
#define _rbcm_h_

#include <string>
#include <vector>
#include <stdint.h>

#include "pfm.h"
#include "../util/dbgout.h"

using namespace std;

#define CURRENT_PF_VERSION 1
#define NUM_FREESPACE_LISTS 11

// The temporary threshold used to determine when we should reorganize pages
#define REORG_THRESHOLD (PAGE_SIZE / 2)

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
  PageNum nextPage;
  unsigned nextSlot;
  bool isAnchor;
} PageIndexSlot;

// Attribute
typedef enum { TypeInt = 0, TypeReal, TypeVarChar } AttrType;
typedef unsigned AttrLength;
struct Attribute {
    string   name;     // attribute name
    AttrType type;     // attribute type
    AttrLength length; // attribute length

  static unsigned sizeInBytes(AttrType type, const void* value);
  static RC allocateValue(AttrType attributeType, const void* valueIn, void** valueOut);
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
struct CorePageIndexHeader
{
  ///////////////
  // layer-specific bytes will go here, above the common core
  ///////////////

  // header metadata
  unsigned freeSpaceOffset;
  unsigned numSlots;
  unsigned gapSize;
  unsigned pageNumber;

  // Doubly linked list data for whichever freespace list we are attached to
  unsigned freespaceList;
  PageNum prevPage;
  PageNum nextPage;
};

class RecordBasedCoreManager
{
public:
  static RecordBasedCoreManager* instance(unsigned slotOffset);

  RC createFile(const string &fileName);
  
  RC destroyFile(const string &fileName);
  
  RC openFile(const string &fileName, FileHandle &fileHandle);
  
  RC closeFile(FileHandle &fileHandle);

  virtual RC readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data);
  
  virtual RC printRecord(const vector<Attribute> &recordDescriptor, const void *data);

  virtual RC deleteRecords(FileHandle &fileHandle);

  virtual RC deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid);

  virtual RC insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid) = 0;

  virtual RC updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid) = 0;

  virtual RC readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data) = 0;

  virtual RC reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber) = 0;

  static PageIndexSlot* getPageIndexSlot(void* pageBuffer, unsigned slotNum, unsigned pageSlotOffset);
  static void writePageIndexSlot(void* pageBuffer, unsigned slotNum, unsigned pageSlotOffset, PageIndexSlot* slot);
  static void* getPageIndexHeader(void* pageBuffer, unsigned pageSlotOffset);
  static int calculateFreespace(unsigned freespaceOffset, unsigned numSlots, unsigned pageSlotOffset);

protected:
  RecordBasedCoreManager(unsigned slotOffset);
  virtual ~RecordBasedCoreManager();

  virtual RC writeHeader(FileHandle &fileHandle, PFHeader* header);
  virtual RC readHeader(FileHandle &fileHandle, PFHeader* header);
  virtual unsigned calcRecordSize(unsigned char* recordBuffer) = 0;

  virtual RC findFreeSpace(FileHandle &fileHandle, unsigned bytes, PageNum& pageNum);
  virtual RC movePageToFreeSpaceList(FileHandle &fileHandle, void* pageHeaderBuffer, unsigned destinationListIndex);
  virtual RC movePageToCorrectFreeSpaceList(FileHandle &fileHandle, void* pageHeaderBuffer);
  
  RC deleteRid(FileHandle& fileHandle, const RID& rid, PageIndexSlot* slotIndex, void* headerBuffer, unsigned char* pageBuffer);
  // RC generateRecordHeader(const vector<Attribute> &recordDescriptor, const void *data, unsigned*& recHeaderOut, unsigned& recLength, unsigned& recHeaderSize);
  // RC reorganizeBufferedPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber, unsigned char* pageBuffer);

private:
  PagedFileManager& _pfm;
  unsigned _pageSlotOffset;
};

#endif /* _rbcm_h_ */