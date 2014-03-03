
#ifndef _ix_h_
#define _ix_h_

#include <vector>
#include <string>
#include <iostream>

#include "../rbf/rbcm.h"

# define IX_EOF (-1)  // end of the index scan

#define MAX_KEY_SIZE 2048

struct IX_PageIndexFooter : CorePageIndexFooter
{
	bool isLeafPage;
	RID firstRecord;

	// Tree pointers
	PageNum parent;
	PageNum nextLeafPage; // ignored by non-leaf pages
	PageNum leftChild;

	friend std::ostream& operator<<(std::ostream& os, const IX_PageIndexFooter& f);
};

struct KeyValueData
{
	int size;

	// This is a super lazy way to not have to dynamically malloc based on the size every time
	// Values in unions 'overlap' so we only use MAX_KEY_SIZE bytes, but we can index into the data as an int or float
	union
	{
		int integer;
		float real;
		char varchar[MAX_KEY_SIZE+sizeof(unsigned)];
	};

	RC init(AttrType type, const void* key);
	RC compare(AttrType type, const KeyValueData& that, int& result);
	void print(AttrType type);
};

struct IndexRecord
{
	RID nextSlot;
	RID rid; // Leaf==dataRid, Non-Leaf==pageRid
	KeyValueData key;

	void print(AttrType type, bool isLeaf);
};

class IX_ScanIterator;
class IndexManager : public RecordBasedCoreManager {
 public:
  static IndexManager* instance();

  // Override parent createFile
  virtual RC createFile(const string &fileName);

	// From RecordBasedCoreManager
	// virtual RC updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid);
	virtual RC readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data);
	virtual RC reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber);

  // The following two functions are using the following format for the passed key value.
  //  1) data is a concatenation of values of the attributes
  //  2) For int and real: use 4 bytes to store the value;
  //     For varchar: use 4 bytes to store the length of characters, then store the actual characters.
  RC insertEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid);  // Insert new index entry
  RC deleteEntry(FileHandle &fileHandle, const Attribute &attribute, const void *key, const RID &rid);  // Delete index entry

  // scan() returns an iterator to allow the caller to go through the results
  // one by one in the range(lowKey, highKey).
  // For the format of "lowKey" and "highKey", please see insertEntry()
  // If lowKeyInclusive (or highKeyInclusive) is true, then lowKey (or highKey)
  // should be included in the scan
  // If lowKey is null, then the range is -infinity to highKey
  // If highKey is null, then the range is lowKey to +infinity
  RC scan(FileHandle &fileHandle,
      const Attribute &attribute,
      const void        *lowKey,
      const void        *highKey,
      bool        lowKeyInclusive,
      bool        highKeyInclusive,
      IX_ScanIterator &ix_ScanIterator);

  static IX_PageIndexFooter* getIXPageIndexFooter(void* pageBuffer);
  static const std::vector<Attribute>& getIndexRecordDescriptor(AttrType type);
  static RC findNonLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, RID& targetRid, RID& prevRid);
  static RC findNonLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, PageNum& pageNum);
  static RC findLeafIndexEntry(FileHandle& fileHandle, const Attribute &attribute, KeyValueData* key, RID& entryRid, RID& prevEntryRid, RID& nextEntryRid, RID& dataRid);
  static RC findLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, RID& entryRid, RID& prevEntryRid, RID& nextEntryRid, RID& dataRid);
  static RC findLeafIndexEntry(FileHandle& fileHandle, IX_PageIndexFooter* footer, const Attribute &attribute, KeyValueData* key, RID& entryRid, RID& dataRid);
  static RC findIndexEntry(FileHandle& fileHandle, const Attribute &attribute, KeyValueData* keyData, RID& entryRid, RID& prevEntryRid, RID& nextEntryRid, RID& dataRid);
  static RC findSmallestLeafIndexEntry(FileHandle& fileHandle, RID& rid);
  static RC readRootPage(FileHandle& fileHandle, void* pageBuffer);
  static RC findLargestLeafIndexEntry(FileHandle& fileHandle, const Attribute& attribute, RID& rid);
  static RC printIndex(FileHandle& fileHandle, const Attribute& attribute, bool extended);
  static RC printIndex(FileHandle& fileHandle, const Attribute& attribute, bool extended, bool restrictToPage, PageNum restrictPage);
  static RC updateRootPage(FileHandle& fileHandle, unsigned newRootPage);

  RC getNextRecord(FileHandle& fileHandle, const std::vector<Attribute>& recordDescriptor, const Attribute& attribute, RID& rid);
  
 protected:
  IndexManager   ();                            // Constructor
  virtual ~IndexManager  ();                    // Destructor

  RC newPage(FileHandle& fileHandle, PageNum pageNum, bool isLeaf, PageNum nextLeafPage, PageNum leftChild);
  RC split(FileHandle& fileHandle, const std::vector<Attribute>& recordDescriptor, PageNum& targetPageNum, PageNum& newPageNum, RID& rightRid, KeyValueData& rightKey);
  RC deletelessSplit(FileHandle& fileHandle, const std::vector<Attribute>& recordDescriptor, PageNum& targetPageNum, PageNum& newPageNum, RID& rightRid, KeyValueData& rightKey);
  RC insertIntoNonLeaf(FileHandle& fileHandle, PageNum& page, const Attribute &attribute, KeyValueData keyData, RID rid);
  RC insertIntoLeaf(FileHandle& fileHandle, PageNum& page, const Attribute &attribute, KeyValueData keyData, RID rid);

 private:
	static IndexManager *_index_manager;
	
	std::vector<Attribute> _indexIntRecordDescriptor;
	std::vector<Attribute> _indexRealRecordDescriptor;
	std::vector<Attribute> _indexVarCharRecordDescriptor;
};

class IX_ScanIterator {
public:
  IX_ScanIterator();  							// Constructor
  ~IX_ScanIterator(); 							// Destructor

  RC getNextEntry(RID &rid, void *key);  		// Get next matching entry
  RC close();             						// Terminate index scan
  RC init(FileHandle* fileHandle, const Attribute &attribute, const void *lowKey, const void *highKey, bool lowKeyInclusive, bool highKeyInclusive);

private:
	RC advance();

	IndexManager& _im;

	FileHandle* _fileHandle;
	Attribute _attribute;
	std::vector<Attribute> _recordDescriptor;
	bool _lowKeyInclusive;
	bool _highKeyInclusive;
	bool _pastLastRecord;
	
	RID _beginRecordRid;
	RID _endRecordRid;
	RID _currentRecordRid;
	RID _nextRecordRid;
};

// print out the error message for a given return code
void IX_PrintError (RC rc);

#endif
