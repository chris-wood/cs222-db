
#ifndef _rm_h_
#define _rm_h_

#include <string>
#include <vector>
#include <map>

#include "../rbf/rbfm.h"
#include "../ix/ix.h"

#define MAX_TABLENAME_SIZE 1024
#define MAX_ATTRIBUTENAME_SIZE 1024

#include <cstring>
using namespace std;

enum TableOwner
{
	TableOwnerSystem = 0,
    TableOwnerUser = 1
};

struct TableMetadataRow
{
	TableMetadataRow() { memset(this, 0, sizeof(*this)); }

	int owner;
	int numAttributes;
	RID nextRow;
	RID prevRow;
	RID firstAttribute;
	char tableName[MAX_TABLENAME_SIZE]; // TODO: We could malloc this and not have this max size
};

struct TableMetaData
{
	FileHandle fileHandle;
	std::vector<Attribute> recordDescriptor;
	RID rowRID;
};

struct AttributeRecord
{
	AttributeRecord() { memset(this, 0, sizeof(*this)); }

	RID nextAttribute;
	AttrType type;
	AttrLength length;
	char name[MAX_ATTRIBUTENAME_SIZE]; // TODO: We could malloc this and not have this max size
};

# define RM_EOF (-1)  // end of a scan operator

// RM_ScanIterator is an iteratr to go through tuples
// The way to use it is like the following:
//  RM_ScanIterator rmScanIterator;
//  rm.open(..., rmScanIterator);
//  while (rmScanIterator(rid, data) != RM_EOF) {
//    process the data;
//  }
//  rmScanIterator.close();

class RM_ScanIterator {
public:
  RM_ScanIterator() {}
  ~RM_ScanIterator() {}

  // "data" follows the same format as RelationManager::insertTuple()
  RC getNextTuple(RID &rid, void *data);
  RC close();

  RBFM_ScanIterator iter;
};

class RM_IndexScanIterator {
public:
	RM_IndexScanIterator() {};  	// Constructor
	~RM_IndexScanIterator() {}; 	// Destructor

	// "key" follows the same format as in IndexManager::insertEntry()
	RC getNextEntry(RID &rid, void *key);
	RC close();

	RC init(TableMetaData& tableData, const string &attributeName, const void *lowKey, const void *highKey, bool lowKeyInclusive, bool highKeyInclusive);

	IX_ScanIterator iter;
	TableMetaData* tableData;
};

// Relation Manager
class RelationManager
{
public:
  static RelationManager* instance();

  RC createTable(const string &tableName, const vector<Attribute> &attrs);

  RC deleteTable(const string &tableName);

  RC getAttributes(const string &tableName, vector<Attribute> &attrs);

  RC insertTuple(const string &tableName, const void *data, RID &rid);

  RC deleteTuples(const string &tableName);

  RC deleteTuple(const string &tableName, const RID &rid);

  // Assume the rid does not change after update
  RC updateTuple(const string &tableName, const void *data, const RID &rid);

  RC readTuple(const string &tableName, const RID &rid, void *data);

  RC readAttribute(const string &tableName, const RID &rid, const string &attributeName, void *data);

  RC reorganizePage(const string &tableName, const unsigned pageNumber);

  // scan returns an iterator to allow the caller to go through the results one by one. 
  RC scan(const string &tableName,
      const string &conditionAttribute,
      const CompOp compOp,                  // comparision type such as "<" and "="
      const void *value,                    // used in the comparison
      const vector<string> &attributeNames, // a list of projected attributes
      RM_ScanIterator &rm_ScanIterator);

  RC createIndex(const string &tableName, const string &attributeName);

  RC destroyIndex(const string &tableName, const string &attributeName);

  // indexScan returns an iterator to allow the caller to go through qualified entries in index
  RC indexScan(const string &tableName,
                        const string &attributeName,
                        const void *lowKey,
                        const void *highKey,
                        bool lowKeyInclusive,
                        bool highKeyInclusive,
                        RM_IndexScanIterator &rm_IndexScanIterator);


// Extra credit
public:
  RC dropAttribute(const string &tableName, const string &attributeName);

  RC addAttribute(const string &tableName, const Attribute &attr);

  RC reorganizeTable(const string &tableName);



protected:
	  RelationManager();
	  ~RelationManager();

private:
    RC createCatalogEntry(const string &tableName, const vector<Attribute> &attrs);
    RC insertTableMetadata(bool isSystemTable, const string &tableName, const vector<Attribute> &attrs);

    RC loadSystemTables();
    RC createSystemTables();
    RC loadTableMetadata();
	RC loadTableColumnMetadata(int numAttributes, RID firstAttributeRID, std::vector<Attribute>& recordDescriptor);

	RecordBasedFileManager* _rbfm;
	std::map<std::string, TableMetaData> _catalog;

	std::vector<Attribute> _systemTableRecordDescriptor;
	std::vector<Attribute> _systemTableAttributeRecordDescriptor;
	RID _lastTableRID;

	static RelationManager *_rm;
};

#endif
