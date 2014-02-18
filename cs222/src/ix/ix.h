
#ifndef _ix_h_
#define _ix_h_

#include <vector>
#include <string>

#include "../rbf/rbcm.h"

# define IX_EOF (-1)  // end of the index scan

#define MAX_KEY_SIZE 2048

struct IX_PageIndexHeader
{
	unsigned isLeafPage;
	RID firstRecord;
	PageNum parent;
  CorePageIndexFooter core;
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
        char varchar[MAX_KEY_SIZE];
    };
};

struct IndexNonLeafRecord
{
	RID pagePointer;
	RID nextSlot;
  KeyValueData key;
};

struct IndexLeafRecord
{
	RID dataRid;
	RID nextSlot;
  KeyValueData key;
};

class IX_ScanIterator;
class IndexManager : public RecordBasedCoreManager {
 public:
  static IndexManager* instance();

  virtual RC createFile(const string &fileName);
  // RC destroyFile(const string &fileName);
  // RC openFile(const string &fileName, FileHandle &fileHandle);
  // RC closeFile(FileHandle &fileHandle);

	// From RecordBasedCoreManager
	virtual RC insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, RID &rid);
	virtual RC updateRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const void *data, const RID &rid);
	virtual RC readAttribute(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const RID &rid, const string attributeName, void *data);
	virtual RC reorganizePage(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor, const unsigned pageNumber);
	virtual unsigned calcRecordSize(unsigned char* recordBuffer);


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

  const std::vector<Attribute>& indexHeaderDescriptor() { return _indexHeaderDescriptor; }
  const std::vector<Attribute>& indexNonLeafRecordDescriptor() { return _indexNonLeafRecordDescriptor; }
  const std::vector<Attribute>& indexLeafRecordDescriptor() { return _indexLeafRecordDescriptor; }

 protected:
  IndexManager   ();                            // Constructor
  virtual ~IndexManager  ();                    // Destructor

  RC newPage(FileHandle& fileHandle, RID& headerRid, PageNum pageNum);

 private:
	static IndexManager *_index_manager;
	
	PagedFileManager& _pfm;

	RID _rootHeaderRid;
	std::vector<Attribute> _indexHeaderDescriptor;
	std::vector<Attribute> _indexNonLeafRecordDescriptor;
	std::vector<Attribute> _indexLeafRecordDescriptor;
};

class IX_ScanIterator {
public:
  IX_ScanIterator();  							// Constructor
  ~IX_ScanIterator(); 							// Destructor

  RC getNextEntry(RID &rid, void *key);  		// Get next matching entry
  RC close();             						// Terminate index scan

  RC init(FileHandle* fileHandle, const Attribute &attribute, const void *lowKey, const void *highKey, bool lowKeyInclusive, bool highKeyInclusive);

private:
	PagedFileManager& _pfm;

	FileHandle* _fileHandle;
	Attribute _attribute;
	void* _lowKeyValue;
	void* _highKeyValue;
	bool _lowKeyInclusive;
	bool _highKeyInclusive;

	RID _currentRid;
};

// print out the error message for a given return code
void IX_PrintError (RC rc);


#endif
