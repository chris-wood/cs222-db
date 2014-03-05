#include "rm.h"
#include "../util/returncodes.h"
#include "../util/hash.h"
#include <assert.h>
#include <sstream>

#define SYSTEM_TABLE_CATALOG_NAME "RM_SYS_CATALOG_TABLE.db"
#define SYSTEM_TABLE_ATTRIUBE_NAME "RM_SYS_ATTRIBUTE_TABLE.db"

RelationManager* RelationManager::_rm = 0;

RelationManager* RelationManager::instance()
{
    if(!_rm)
        _rm = new RelationManager();

    return _rm;
}

RelationManager::RelationManager()
	: _lastTableRID()
{
	_rbfm = RecordBasedFileManager::instance();
	assert(_rbfm);

	// Columns of the system table
	Attribute attr;
	attr.type = TypeInt;
	attr.length = sizeof(int);
	attr.name = "Owner";						_systemTableRecordDescriptor.push_back(attr);
	attr.name = "NumAttributes";				_systemTableRecordDescriptor.push_back(attr);
    attr.name = "NextRowRID_page";				_systemTableRecordDescriptor.push_back(attr);
    attr.name = "NextRowRID_slot";				_systemTableRecordDescriptor.push_back(attr);
    attr.name = "PrevRowRID_page";				_systemTableRecordDescriptor.push_back(attr);
    attr.name = "PrevRowRID_slot";				_systemTableRecordDescriptor.push_back(attr);
	attr.name = "RecordDescriptorRID_page";		_systemTableRecordDescriptor.push_back(attr);
	attr.name = "RecordDescriptorRID_slot";		_systemTableRecordDescriptor.push_back(attr);
	attr.name = "TableName";
	attr.type = TypeVarChar;
	attr.length = MAX_TABLENAME_SIZE;
	_systemTableRecordDescriptor.push_back(attr);

	// Columns of the attribute list
	attr.type = TypeInt;
	attr.length = sizeof(int);
	attr.name = "NextAttributeRID_page";		_systemTableAttributeRecordDescriptor.push_back(attr);
	attr.name = "NextAttributeRID_slot";		_systemTableAttributeRecordDescriptor.push_back(attr);
	attr.name = "Type";							_systemTableAttributeRecordDescriptor.push_back(attr);
	attr.name = "Length";						_systemTableAttributeRecordDescriptor.push_back(attr);
	attr.name = "TableName";
	attr.type = TypeVarChar;
	attr.length = MAX_ATTRIBUTENAME_SIZE;
	_systemTableAttributeRecordDescriptor.push_back(attr);
	
	// Generate the system table which will hold data about all other created tables
	ASSERT_ON_BAD_RETURN = false;
    if (loadSystemTables() != rc::OK)
    {
		ASSERT_ON_BAD_RETURN = GLOBAL_ASSERT_ON_BAD_RETURN;
        RC ret = createSystemTables();
        assert(ret == rc::OK);
    }

	// Read in data about already known tables
	RC ret = loadTableMetadata();
	assert(ret == rc::OK);
}

RelationManager::~RelationManager()
{
	_rm = NULL;

	for (std::map<std::string, TableMetaData>::iterator it = _catalog.begin(); it != _catalog.end(); ++it)
	{
		_rbfm->closeFile( ((*it).second).fileHandle );
	}
}

RC RelationManager::loadSystemTables()
{
    RC ret = rc::OK;

    // If table exists in our catalog, it has been created and we know about it already
    if (_catalog.find(SYSTEM_TABLE_CATALOG_NAME) != _catalog.end() || _catalog.find(SYSTEM_TABLE_ATTRIUBE_NAME) != _catalog.end())
    {
        return rc::TABLE_ALREADY_CREATED;
    }

    // Attempt to open the system catalog if it already exists
    _catalog[SYSTEM_TABLE_CATALOG_NAME] = TableMetaData();
    ret = _rbfm->openFile(SYSTEM_TABLE_CATALOG_NAME, _catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle);
    RETURN_ON_ERR(ret);

    // Attempt to open the attribute table if it exists
    _catalog[SYSTEM_TABLE_ATTRIUBE_NAME] = TableMetaData();
    ret = _rbfm->openFile(SYSTEM_TABLE_ATTRIUBE_NAME, _catalog[SYSTEM_TABLE_ATTRIUBE_NAME].fileHandle);
    RETURN_ON_ERR(ret);

    return rc::OK;
}

RC RelationManager::createSystemTables()
{
    RC ret = rc::OK;

    // Create the system tables
    ret = createCatalogEntry(SYSTEM_TABLE_CATALOG_NAME, _systemTableRecordDescriptor);
    RETURN_ON_ERR(ret);

    ret = createCatalogEntry(SYSTEM_TABLE_ATTRIUBE_NAME, _systemTableAttributeRecordDescriptor);
    RETURN_ON_ERR(ret);

    // Write out entries in the system tables for the system data
    ret = insertTableMetadata(true, SYSTEM_TABLE_CATALOG_NAME, _systemTableRecordDescriptor);
    RETURN_ON_ERR(ret);

    ret = insertTableMetadata(true, SYSTEM_TABLE_ATTRIUBE_NAME, _systemTableAttributeRecordDescriptor);
    RETURN_ON_ERR(ret);

    return rc::OK;
}

RC RelationManager::createCatalogEntry(const string &tableName, const vector<Attribute> &attrs)
{
    RC ret = _rbfm->createFile(tableName);
    RETURN_ON_ERR(ret);

    // Save a file handle for the table
    _catalog[tableName] = TableMetaData();
    ret = _rbfm->openFile(tableName, _catalog[tableName].fileHandle);
    RETURN_ON_ERR(ret);

    // Copy over the given record descriptor to local memory
    for (vector<Attribute>::const_iterator it = attrs.begin(); it != attrs.end(); ++it)
    {
        _catalog[tableName].recordDescriptor.push_back(*it);
    }

    return rc::OK;
}

RC RelationManager::insertTableMetadata(bool isSystemTable, const string &tableName, const vector<Attribute> &attrs)
{
    RID prevLastRID = _lastTableRID;

    // Insert table metadata into the system table
    TableMetadataRow newRow;
    newRow.prevRow = prevLastRID;
    newRow.nextRow.pageNum = 0;
    newRow.nextRow.slotNum = 0;
    newRow.firstAttribute.pageNum = 0;
    newRow.firstAttribute.slotNum = 0;
    newRow.numAttributes = attrs.size();
    newRow.owner = isSystemTable ? TableOwnerSystem : TableOwnerUser;

    int tableNameLen = tableName.size();
    if (tableNameLen >= MAX_TABLENAME_SIZE)
    {
        return rc::TABLE_NAME_TOO_LONG;
    }

    memcpy(newRow.tableName, &tableNameLen, sizeof(int));
    memcpy(newRow.tableName + sizeof(int), tableName.c_str(), tableNameLen);

    RC ret = _rbfm->insertRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, &newRow, _lastTableRID);
    RETURN_ON_ERR(ret);

    // Store the RID of this row in the in-memory catalog for easy access
    _catalog[tableName].rowRID = _lastTableRID;

    // Insert the column attributes (we iterate backwards se we can populate the 'next' pointers with the previsouly written RID. this saves us having to go back and update the next pointer RIDs after one pass)
    RID attributeRID;
    attributeRID.pageNum = 0;
    attributeRID.slotNum = 0;

    for (vector<Attribute>::const_reverse_iterator it = attrs.rbegin(); it != attrs.rend(); ++it)
    {
        const Attribute& attr = *it;

        // Allocate space for the RID that points to the next attribute, the attribute type, the attribute size, and the attribute name
        AttributeRecord attrRec;
        attrRec.type = attr.type;
        attrRec.length = attr.length;
        attrRec.nextAttribute.pageNum = attributeRID.pageNum;
        attrRec.nextAttribute.slotNum = attributeRID.slotNum;

        int nameLen = attr.name.length();
        if (nameLen >= MAX_ATTRIBUTENAME_SIZE)
        {
            return rc::ATTRIBUTE_NAME_TOO_LONG;
        }

        memcpy(attrRec.name, &nameLen, sizeof(int));
        memcpy(attrRec.name + sizeof(int), attr.name.c_str(), nameLen);

        // Write out the record with the attribute data
        ret = _rbfm->insertRecord(_catalog[SYSTEM_TABLE_ATTRIUBE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, &attrRec, attributeRID);
        RETURN_ON_ERR(ret);
    }

    // Update the row data with the pointer to its first attribute and store the RID so we have an easy way to delete later on
    newRow.firstAttribute = attributeRID;
    ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, &newRow, _lastTableRID);
    RETURN_ON_ERR(ret);

    // If there was a previous row, we need to update its next pointer
    if (prevLastRID.pageNum > 0)
    {
        TableMetadataRow prevRow;
        ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, prevLastRID, &prevRow);
        RETURN_ON_ERR(ret);

        prevRow.nextRow.pageNum = _lastTableRID.pageNum;
        prevRow.nextRow.slotNum = _lastTableRID.slotNum;

        ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, &prevRow, prevLastRID);
        RETURN_ON_ERR(ret);
    }

    return rc::OK;
}

RC RelationManager::createTable(const string &tableName, const vector<Attribute> &attrs)
{
	RC ret = rc::OK;

	// If table exists in our catalog, it has been created and we know about it already
	if (_catalog.find(tableName) != _catalog.end())
	{
		return rc::TABLE_ALREADY_CREATED;
	}

	// Delete any existing table to create a fresh one
    _rbfm->destroyFile(tableName);
    ret = createCatalogEntry(tableName, attrs);
    RETURN_ON_ERR(ret);

    ret = insertTableMetadata(false, tableName, attrs);
    RETURN_ON_ERR(ret);

    return rc::OK;
}

RC RelationManager::loadTableMetadata()
{
	RC ret = rc::OK;
	RID currentRID;
	TableMetadataRow currentRow;

	// Load in the 1st record, which should have our system table row info
	currentRID.pageNum = 1;
	currentRID.slotNum = 0;
    _catalog[SYSTEM_TABLE_CATALOG_NAME].rowRID = currentRID;
    ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, currentRID, &currentRow);
	RETURN_ON_ERR(ret);

    // Load in all of the system table rows
	while (currentRID.pageNum > 0)
	{
		// Read in the next row metadata
		memset(&currentRow, 0, sizeof(currentRow));
        ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, currentRID, &currentRow);
		RETURN_ON_ERR(ret);

        // Clear out any old data we have about this table
        const std::string tableName((char*)(currentRow.tableName + sizeof(int)));
        _catalog[tableName].recordDescriptor.clear();

        // Load in the attribute table data for this row
		ret = loadTableColumnMetadata(currentRow.numAttributes, currentRow.firstAttribute, _catalog[tableName].recordDescriptor);
		RETURN_ON_ERR(ret);

        // Open a file handle for the table
        if (tableName != SYSTEM_TABLE_CATALOG_NAME && tableName != SYSTEM_TABLE_ATTRIUBE_NAME)
        {
            ret = _rbfm->openFile(tableName, _catalog[tableName].fileHandle);
            RETURN_ON_ERR(ret);
        }

		// Store the RID in our in-memory catalog for easy access
		_catalog[tableName].rowRID = currentRID;

		// Advance to the next row to read in more table data
		currentRID = currentRow.nextRow;
	}

	return rc::OK;
}

RC RelationManager::loadTableColumnMetadata(int numAttributes, RID firstAttributeRID, std::vector<Attribute>& recordDescriptor)
{
	RC ret = rc::OK;
	int readAttributes = 0;
	RID currentRID = firstAttributeRID;

	AttributeRecord attrRec;
	while (currentRID.pageNum > 0)
	{
		// Null out data structure so we will have a null terminated string
		memset(&attrRec, 0, sizeof(attrRec));

		// Read in the attribute record
        ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_ATTRIUBE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, currentRID, &attrRec);
		RETURN_ON_ERR(ret);

		// Copy over the data into a regular attribute and add this attribute to the recordDescriptor 
		Attribute attr;
		attr.length = attrRec.length;
		attr.type = attrRec.type;
        attr.name = std::string((char*)(attrRec.name + sizeof(int)));
		recordDescriptor.push_back(attr);

		// Move on to the next attribute
		currentRID.pageNum = attrRec.nextAttribute.pageNum;
		currentRID.slotNum = attrRec.nextAttribute.slotNum;

		++readAttributes;
	}

	if (readAttributes != numAttributes)
	{
		return rc::ATTRIBUTE_COUNT_MISMATCH;
	}

	return rc::OK;
}

RC RelationManager::deleteTable(const string &tableName)
{
	std::map<std::string, TableMetaData>::iterator it = _catalog.find(tableName);
	if (it == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	RC ret = _rbfm->closeFile(it->second.fileHandle);
	RETURN_ON_ERR(ret);

	ret = _rbfm->destroyFile(tableName);
	RETURN_ON_ERR(ret);

	// Load in the row that is to be deleted
	TableMetadataRow currentRow;
    ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, it->second.rowRID, &currentRow);
	RETURN_ON_ERR(ret);

	// Delete the attributes stored along with this table
	int deletedAttributes = 0;
	AttributeRecord attributeBuffer;
	RID attributeRid = currentRow.firstAttribute;
	while(attributeRid.pageNum > 0 && deletedAttributes <= currentRow.numAttributes)
	{
        //RID prevAttribute = attributeRid;
		memset(&attributeBuffer, 0, sizeof(attributeBuffer));

		// Read in the attribute so we can continue along the chain
		ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_ATTRIUBE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, attributeRid, &attributeBuffer);
		RETURN_ON_ERR(ret);

		// Delete the current attribute
		++deletedAttributes;
		ret = _rbfm->deleteRecord(_catalog[SYSTEM_TABLE_ATTRIUBE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, attributeRid);
		RETURN_ON_ERR(ret);

		// Determine the next attribute
		attributeRid = attributeBuffer.nextAttribute;
	}
	
	// Load in the previous row, we will need to update its next pointer
	if (currentRow.prevRow.pageNum > 0)
	{
		TableMetadataRow prevRow;
        ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, currentRow.prevRow, &prevRow);
		RETURN_ON_ERR(ret);

		// Previous row will point to whatever our next row is
		prevRow.nextRow = currentRow.nextRow;
        ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, &prevRow, currentRow.prevRow);
		RETURN_ON_ERR(ret);
	}

	// Load in the next row, we will need to update its prev pointer
	if (currentRow.nextRow.pageNum > 0)
	{
		TableMetadataRow nextRow;
        ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, currentRow.nextRow, &nextRow);
		RETURN_ON_ERR(ret);

		// Next row will point to whatever our previous row was
		nextRow.prevRow = currentRow.prevRow;
        ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, &nextRow, currentRow.nextRow);
		RETURN_ON_ERR(ret);
	}

	// If this row was the last one, update our lastTableRID
	if (it->second.rowRID.pageNum == _lastTableRID.pageNum && it->second.rowRID.slotNum == _lastTableRID.slotNum)
	{
		_lastTableRID = currentRow.prevRow;
	}
	
	// Now delete the old table record from disk
    ret = _rbfm->deleteRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, it->second.rowRID);
	RETURN_ON_ERR(ret);

    // TODO: Delete stuff from the SYSTEM_TABLE_ATTRIBUTE_NAME table also

	// Finally, update our in-memory representation of the catalog
	_catalog.erase(it);

	return rc::OK;
}

RC RelationManager::getAttributes(const string &tableName, vector<Attribute> &attrs)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	attrs.clear();

	TableMetaData& tableData = _catalog[tableName];
	for (vector<Attribute>::const_iterator it = tableData.recordDescriptor.begin(); it != tableData.recordDescriptor.end(); ++it)
	{
		attrs.push_back(*it);
	}

	return rc::OK;
}

RC RelationManager::insertTuple(const string &tableName, const void *data, RID &rid)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->insertRecord(tableData.fileHandle, tableData.recordDescriptor, data, rid);
}

RC RelationManager::deleteTuples(const string &tableName)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->deleteRecords(tableData.fileHandle);
}

RC RelationManager::deleteTuple(const string &tableName, const RID &rid)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->deleteRecord(tableData.fileHandle, tableData.recordDescriptor, rid);
}

RC RelationManager::updateTuple(const string &tableName, const void *data, const RID &rid)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->updateRecord(tableData.fileHandle, tableData.recordDescriptor, data, rid);
}

RC RelationManager::readTuple(const string &tableName, const RID &rid, void *data)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->readRecord(tableData.fileHandle, tableData.recordDescriptor, rid, data);
}

RC RelationManager::readAttribute(const string &tableName, const RID &rid, const string &attributeName, void *data)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->readAttribute(tableData.fileHandle, tableData.recordDescriptor, rid, attributeName, data);
}

RC RelationManager::reorganizePage(const string &tableName, const unsigned pageNumber)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->reorganizePage(tableData.fileHandle, tableData.recordDescriptor, pageNumber);
}

std::string RelationManager::getIndexName(const string& baseTable, const string& attributeName)
{
	std::stringstream out;
	out << baseTable;
	out << '.';
	//out << util::strhash(attributeName);
	out << attributeName;

	return out.str();
}

RC RelationManager::scan(const string &tableName,
      const string &conditionAttribute,
      const CompOp compOp,                  
      const void *value,                    
      const vector<string> &attributeNames,
      RM_ScanIterator &rm_ScanIterator)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->scan(
        tableData.fileHandle,
		tableData.recordDescriptor, 
		conditionAttribute, 
		compOp, 
		value, 
		attributeNames, 
		rm_ScanIterator.iter);
}

RC RM_ScanIterator::getNextTuple(RID &rid, void *data)
{
	return iter.getNextRecord(rid, data);
}

RC RM_ScanIterator::close()
{
	return iter.close();
}

RC RM_IndexScanIterator::getNextEntry(RID &rid, void *key)
{
	return iter.getNextEntry(rid, key);
}

RC RM_IndexScanIterator::close()
{
	tableData = NULL;
	return iter.close();
}

RC RM_IndexScanIterator::init(TableMetaData& _tableData, const string &attributeName, const void *lowKey, const void *highKey, bool lowKeyInclusive, bool highKeyInclusive)
{
	tableData = &_tableData;
	
	unsigned attributeIndex = 0;
	RC ret = RBFM_ScanIterator::findAttributeByName(tableData->recordDescriptor, attributeName, attributeIndex);
	RETURN_ON_ERR(ret);

	return iter.init(&tableData->fileHandle, tableData->recordDescriptor[attributeIndex], lowKey, highKey, lowKeyInclusive, highKeyInclusive);
}

RC RelationManager::createIndex(const string &tableName, const string &attributeName)
{
	IndexManager* im = IndexManager::instance();

	// Create the file that will hold our index
	const std::string indexName = getIndexName(tableName, attributeName);
	RC ret = im->createFile(indexName);
	RETURN_ON_ERR(ret);

	FileHandle indexFileHandle;
	ret = im->openFile(indexName, indexFileHandle);
	RETURN_ON_ERR(ret);

	// TODO: Keep track that we have created this index in some system table?
	
	// Load in the table we are creating the index for
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];

	// We only want to extract the attribute for this index
	std::vector<std::string> attributeNames;
	attributeNames.push_back(attributeName);

	// Pull out the attribute data from the attribute name
	unsigned attributeIndex = 0;
	ret = RBFM_ScanIterator::findAttributeByName(tableData.recordDescriptor, attributeName, attributeIndex);
	RETURN_ON_ERR(ret);
	
	RM_ScanIterator scanner;
	ret = scan(tableName, attributeName, NO_OP, NULL, attributeNames, scanner);
	RETURN_ON_ERR(ret);

	// Iterate through everything in the input table and add entries into the new index
	RID rid;
	char tupleBuffer[PAGE_SIZE] = {0};
	while((ret = scanner.getNextTuple(rid, tupleBuffer)) == rc::OK)
	{
		// Insert the value into our index now
		ret = im->insertEntry(indexFileHandle, tableData.recordDescriptor[attributeIndex], tupleBuffer, rid);
		RETURN_ON_ERR(ret);

		memset(tupleBuffer, 0, PAGE_SIZE);
	}

	return rc::OK;
}

RC RelationManager::destroyIndex(const string &tableName, const string &attributeName)
{
	RC ret = IndexManager::instance()->destroyFile(getIndexName(tableName, attributeName));
	RETURN_ON_ERR(ret);

	// TODO: Remove any system table data we saved for this index

	return rc::OK;
}

RC RelationManager::indexScan(const string &tableName,
	const string &attributeName,
	const void *lowKey,
	const void *highKey,
	bool lowKeyInclusive,
	bool highKeyInclusive,
	RM_IndexScanIterator &rm_IndexScanIterator)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];

	return rm_IndexScanIterator.init(tableData, attributeName, lowKey, highKey, lowKeyInclusive, highKeyInclusive);
}

// Extra credit
RC RelationManager::dropAttribute(const string &tableName, const string &/*attributeName*/)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	// TableMetaData& tableData = _catalog[tableName];

	// TODO: Write me
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

// Extra credit
RC RelationManager::addAttribute(const string &tableName, const Attribute &/*attr*/)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	// TableMetaData& tableData = _catalog[tableName];

	// TODO: Write me
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

// Extra credit
RC RelationManager::reorganizeTable(const string &tableName)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	return _rbfm->reorganizeFile(tableData.fileHandle, tableData.recordDescriptor);
}
