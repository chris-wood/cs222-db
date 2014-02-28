#ifndef _rbcm_h_
#define _rbcm_h_

#include <string>
#include <vector>
#include <stdint.h>
#include <iostream>

#include "pfm.h"
#include "../util/dbgout.h"

using namespace std;

#define CURRENT_PF_VERSION 2
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
struct RID
{
  PageNum pageNum;
  unsigned slotNum;

  RID() : pageNum(0), slotNum(0) {}

  friend std::ostream& operator<<(std::ostream& os, const RID& r);
};

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

  friend std::ostream& operator<<(std::ostream& os, const Attribute& a);
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
struct CorePageIndexFooter
{
  // header metadata
  unsigned freeSpaceOffset;
  unsigned numSlots;
  unsigned gapSize;
  unsigned pageNumber;

  // Doubly linked list data for whichever freespace list we are attached to
  unsigned freespaceList;
  PageNum freespacePrevPage;
  PageNum freespaceNextPage;
};

class RecordBasedCoreManager
{
public:
  static RecordBasedCoreManager* instance(unsigned slotOffset);

  // File operations
  virtual RC createFile(const string &fileName);
  virtual RC destroyFile(const string &fileName);
  virtual RC openFile(const string &fileName, FileHandle &fileHandle);
  virtual RC closeFile(FileHandle &fileHandle);

  // Generalized record operations
  virtual RC readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data);
  virtual RC readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data, void* pageBuffer);
  virtual RC printRecord(const vector<Attribute> &recordDescriptor, const void *data);
  virtual RC deleteRecords(FileHandle &fileHandle);
  virtual RC deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid);
  virtual RC insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid);
  virtual RC updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid);

  // Additional API for part 3
  RC insertRecordToPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, PageNum pageNum, RID &rid);
  RC insertRecordInplace(const vector<Attribute> &recordDescriptor, const void *data, PageNum pageNum, void* pageBuffer, RID &rid);
  RC updateRecordInplace(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid, void* pageBuffer);
  RC deleteRecordInplace(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void* pageBuffer);

  // Methods delegated to the children
  virtual RC readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data) = 0;
  virtual RC reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber) = 0;

  static PageIndexSlot* getPageIndexSlot(void* pageBuffer, unsigned slotNum, unsigned pageSlotOffset);
  static void writePageIndexSlot(void* pageBuffer, unsigned slotNum, unsigned pageSlotOffset, PageIndexSlot* slot);
  static void* getPageIndexFooter(void* pageBuffer, unsigned pageSlotOffset);
  static unsigned calculateFreespace(unsigned freespaceOffset, unsigned numSlots, unsigned pageSlotOffset);

protected:
  RecordBasedCoreManager(unsigned slotOffset);
  virtual ~RecordBasedCoreManager();

  virtual RC writeHeader(FileHandle &fileHandle, PFHeader* header);
  virtual RC readHeader(FileHandle &fileHandle, PFHeader* header);
  unsigned calcRecordSize(unsigned char* recordBuffer);

  virtual RC findFreeSpace(FileHandle &fileHandle, unsigned bytes, PageNum& pageNum);
  virtual RC movePageToFreeSpaceList(FileHandle &fileHandle, void* pageFooterBuffer, unsigned destinationListIndex);
  virtual RC movePageToCorrectFreeSpaceList(FileHandle &fileHandle, void* pageFooterBuffer);
  
  RC deleteRid(FileHandle& fileHandle, const RID& rid, PageIndexSlot* slotIndex, void* pageFooterBuffer, unsigned char* pageBuffer);

  virtual RC generateRecordHeader(const vector<Attribute> &recordDescriptor, const void *data, unsigned*& recHeaderOut, unsigned& recLength, unsigned& recHeaderSize);
  
  CorePageIndexFooter* getCorePageIndexFooter(void* pageBuffer);
  PageIndexSlot* getPageIndexSlot(void* pageBuffer, unsigned slotNum);
  void writePageIndexSlot(void* pageBuffer, unsigned slotNum, PageIndexSlot* slot);
  virtual unsigned calculateFreespace(unsigned freespaceOffset, unsigned numSlots);
  RC reorganizeBufferedPage(FileHandle &fileHandle, unsigned footerSize, const vector<Attribute> &recordDescriptor, const unsigned pageNumber, unsigned char* pageBuffer);

private:
	PagedFileManager& _pfm;
	unsigned _pageSlotOffset;
};

#endif /* _rbcm_h_ */
