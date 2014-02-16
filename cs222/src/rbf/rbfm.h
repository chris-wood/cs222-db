#ifndef _rbfm_h_
#define _rbfm_h_

#include "rbcm.h"
#include "../util/dbgout.h"

struct PageIndexHeader
{
  // new stuff goes here

  unsigned freeSpaceOffset;
  unsigned numSlots;
  unsigned gapSize;
  unsigned pageNumber;

  // Doubly linked list data for whichever freespace list we are attached to
  unsigned freespaceList;
  PageNum prevPage;
  PageNum nextPage;
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
    RBFM_ScanIterator() : _fileHandle(NULL), _comparasionValue(NULL), _conditionAttributeIndex(-1) {}
	~RBFM_ScanIterator() { if (_comparasionValue) { free(_comparasionValue); } }

	// "data" follows the same format as RecordBasedFileManager::insertRecord()
	RC getNextRecord(RID& rid, void* data);
	RC close();
	
    RC init(FileHandle& fileHandle, const vector<Attribute> &recordDescriptor, const string &conditionAttributeString, const CompOp compOp, const void *value, const vector<string> &attributeNames);

private:
	static bool compareInt(CompOp op, void* a, void* b);
	static bool compareReal(CompOp op, void* a, void* b);
	static bool compareVarChar(CompOp op, void* a, void* b);
	static RC findAttributeByName(const vector<Attribute>& recordDescriptor, const string& conditionAttribute, unsigned& index);

	void nextRecord(unsigned numSlots);
	RC allocateValue(AttrType attributeType, const void* value);
	void copyRecord(char* data, const char* record, unsigned numAttributes);
	bool recordMatchesValue(char* record);

  FileHandle* _fileHandle;
	RID _nextRid;

	CompOp _comparasionOp;
	void* _comparasionValue;
	AttrType _conditionAttributeType;
	unsigned _conditionAttributeIndex;
	std::vector<unsigned> _returnAttributeIndices;
	std::vector<AttrType> _returnAttributeTypes;
};


class RecordBasedFileManager : public RecordBasedCoreManager
{
public:
  static RecordBasedFileManager* instance();

  //  Format of the data passed into the function is the following:
  //  1) data is a concatenation of values of the attributes
  //  2) For int and real: use 4 bytes to store the value;
  //     For varchar: use 4 bytes to store the length of characters, then store the actual characters.
  //  !!!The same format is used for updateRecord(), the returned data of readRecord(), and readAttribute()
  RC insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid);
  
  // RC readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, void *data);
  
  // This method will be mainly used for debugging/testing
  // RC printRecord(const vector<Attribute> &recordDescriptor, const void *data);

  // RC deleteRecords(FileHandle &fileHandle);

  // RC deleteRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid);

  // Assume the rid does not change after update
  RC updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid);

  RC readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data);

  RC reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber);

  // scan returns an iterator to allow the caller to go through the results one by one. 
  RC scan(FileHandle& fileHandle,
      const vector<Attribute> &recordDescriptor,
      const string &conditionAttribute,
      const CompOp compOp,                  // comparision type such as "<" and "="
      const void *value,                    // used in the comparison
      const vector<string> &attributeNames, // a list of projected attributes
      RBFM_ScanIterator &rbfm_ScanIterator);


// Extra credit for part 2 of the project, please ignore for part 1 of the project
public:

  RC reorganizeFile(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor);

  // static PageIndexSlot* getPageIndexSlot(void* pageBuffer, unsigned slotNum);
  // static void writePageIndexSlot(void* pageBuffer, unsigned slotNum, PageIndexSlot* slot);
  // static PageIndexHeader* getPageIndexHeader(void* pageBuffer);
  // static int calculateFreespace(unsigned freespaceOffset, unsigned numSlots);

protected:
  RecordBasedFileManager();
  virtual ~RecordBasedFileManager();

  // RC findFreeSpace(FileHandle &fileHandle, unsigned bytes, PageNum& pageNum);
  
  // RC writeHeader(FileHandle &fileHandle, PFHeader* header);
  // RC readHeader(FileHandle &fileHandle, PFHeader* header);
  
  // RC movePageToFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader, unsigned destinationListIndex);
  // RC movePageToCorrectFreeSpaceList(FileHandle &fileHandle, PageIndexHeader& pageHeader);
  unsigned calcRecordSize(unsigned char* recordBuffer);

  // RC deleteRid(FileHandle& fileHandle, const RID& rid, PageIndexSlot* slotIndex, PageIndexHeader* header, unsigned char* pageBuffer);

  RC generateRecordHeader(const vector<Attribute> &recordDescriptor, const void *data, unsigned*& recHeaderOut, unsigned& recLength, unsigned& recHeaderSize);
  RC reorganizeBufferedPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber, unsigned char* pageBuffer);

private:
  static RecordBasedFileManager *_rbf_manager;
  PagedFileManager& _pfm;
};


#endif
