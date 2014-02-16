#ifndef _rbfm_h_
#define _rbfm_h_

#include "rbcm.h"
#include "../util/dbgout.h"

struct RBFM_PageIndexFooter : public CorePageIndexFooter
{
};

# define RBFM_EOF (-1)  // end of a scan operator

// RBFM_ScanIterator is an iteratr to go through records
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

public:
	RC reorganizeFile(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor);

	// Additional API for part 3 of the project, consumer is the Indexing Manager
	RC freespaceOnPage(FileHandle& fileHandle, PageNum pageNum, int& freespace);
	RC insertRecordToPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, PageNum pageNum, RID &rid);

protected:
	RecordBasedFileManager();
	virtual ~RecordBasedFileManager();

	unsigned calcRecordSize(unsigned char* recordBuffer);
	
	RC generateRecordHeader(const vector<Attribute> &recordDescriptor, const void *data, unsigned*& recHeaderOut, unsigned& recLength, unsigned& recHeaderSize);
	RC reorganizeBufferedPage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber, unsigned char* pageBuffer);

	RBFM_PageIndexFooter* getRBFMPageIndexFooter(void* pageBuffer);

private:
	static RecordBasedFileManager *_rbf_manager;
	PagedFileManager& _pfm;
};


#endif
