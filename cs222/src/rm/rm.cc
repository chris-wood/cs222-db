#include "rm.h"
#include "../util/returncodes.h"
#include "../util/hash.h"
#include <assert.h>
#include <sstream>

#define SYSTEM_TABLE_CATALOG_NAME "RM_SYS_CATALOG_TABLE.db"
#define SYSTEM_TABLE_ATTRIBUTE_NAME "RM_SYS_ATTRIBUTE_TABLE.db"
#define SYSTEM_TABLE_INDEX_NAME "RM_SYS_INDEX_TABLE.db"

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
	attr.name = "FirstIndexRID_page";			_systemTableRecordDescriptor.push_back(attr);
	attr.name = "FirstIndexRID_slot";			_systemTableRecordDescriptor.push_back(attr);
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

	// Columns of the index table
	attr.type = TypeInt;
	attr.length = sizeof(int);
	attr.name = "NextIndexRID_page";			_systemTableIndexRecordDescriptor.push_back(attr);
	attr.name = "NextIndexRID_slot";			_systemTableIndexRecordDescriptor.push_back(attr);
	attr.type = TypeVarChar;
	attr.length = MAX_ATTRIBUTENAME_SIZE;
	attr.name = "SourceTable";					_systemTableIndexRecordDescriptor.push_back(attr);
	attr.name = "FileName";						_systemTableIndexRecordDescriptor.push_back(attr);
	attr.name = "AttrName";						_systemTableIndexRecordDescriptor.push_back(attr);

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
	if (_catalog.find(SYSTEM_TABLE_CATALOG_NAME) != _catalog.end() && _catalog.find(SYSTEM_TABLE_ATTRIBUTE_NAME) != _catalog.end() && _catalog.find(SYSTEM_TABLE_INDEX_NAME) != _catalog.end())
    {
        return rc::TABLE_ALREADY_CREATED;
    }

	// If one or more but not all are in our catalog, something went very wrong
	if (_catalog.find(SYSTEM_TABLE_CATALOG_NAME) != _catalog.end() || _catalog.find(SYSTEM_TABLE_ATTRIBUTE_NAME) != _catalog.end() || _catalog.find(SYSTEM_TABLE_INDEX_NAME) != _catalog.end())
    {
        return rc::TABLE_SYSTEM_IN_BAD_STATE;
    }

    // Attempt to open the system catalog if it already exists
    _catalog[SYSTEM_TABLE_CATALOG_NAME] = TableMetaData();
    ret = _rbfm->openFile(SYSTEM_TABLE_CATALOG_NAME, _catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle);
    RETURN_ON_ERR(ret);

    // Attempt to open the attribute table if it exists
    _catalog[SYSTEM_TABLE_ATTRIBUTE_NAME] = TableMetaData();
    ret = _rbfm->openFile(SYSTEM_TABLE_ATTRIBUTE_NAME, _catalog[SYSTEM_TABLE_ATTRIBUTE_NAME].fileHandle);
    RETURN_ON_ERR(ret);

	// Attempt to open the index table if it exists
	_catalog[SYSTEM_TABLE_INDEX_NAME] = TableMetaData();
	ret = _rbfm->openFile(SYSTEM_TABLE_INDEX_NAME, _catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle);
    RETURN_ON_ERR(ret);

    return rc::OK;
}

RC RelationManager::createSystemTables()
{
    RC ret = rc::OK;

    // Create the system tables
    ret = createCatalogEntry(SYSTEM_TABLE_CATALOG_NAME, _systemTableRecordDescriptor);
    RETURN_ON_ERR(ret);

    ret = createCatalogEntry(SYSTEM_TABLE_ATTRIBUTE_NAME, _systemTableAttributeRecordDescriptor);
    RETURN_ON_ERR(ret);

	ret = createCatalogEntry(SYSTEM_TABLE_INDEX_NAME, _systemTableIndexRecordDescriptor);
    RETURN_ON_ERR(ret);

    // Write out entries in the system tables for the system data
    ret = insertTableMetadata(true, SYSTEM_TABLE_CATALOG_NAME, _systemTableRecordDescriptor);
    RETURN_ON_ERR(ret);

    ret = insertTableMetadata(true, SYSTEM_TABLE_ATTRIBUTE_NAME, _systemTableAttributeRecordDescriptor);
    RETURN_ON_ERR(ret);

	ret = insertTableMetadata(true, SYSTEM_TABLE_INDEX_NAME, _systemTableIndexRecordDescriptor);
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
    newRow.firstIndex.pageNum = 0;
    newRow.firstIndex.slotNum = 0;
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
        ret = _rbfm->insertRecord(_catalog[SYSTEM_TABLE_ATTRIBUTE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, &attrRec, attributeRID);
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
		if (tableName != SYSTEM_TABLE_CATALOG_NAME && tableName != SYSTEM_TABLE_ATTRIBUTE_NAME && tableName != SYSTEM_TABLE_INDEX_NAME)
        {
            ret = _rbfm->openFile(tableName, _catalog[tableName].fileHandle);
            RETURN_ON_ERR(ret);
        }

		// Store the RID in our in-memory catalog for easy access
		_catalog[tableName].rowRID = currentRID;

		// Advance to the next row to read in more table data
		currentRID = currentRow.nextRow;

		// Now read in the indices for this 
		IndexSystemRecord indexRow;
		RID indexRid = currentRow.firstIndex;
		while (indexRid.pageNum > 0)
		{
			memset(&indexRow, 0, sizeof(indexRow));
			ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, indexRid, &indexRow);
			RETURN_ON_ERR(ret);

			int offset = 0;
			int sourceNameLen = 0;
			memcpy(&sourceNameLen, indexRow.buffer + offset, sizeof(int));
			char* pulledTableName = (char*)malloc(sourceNameLen + 1);
			memcpy(pulledTableName, indexRow.buffer + offset + sizeof(int), sourceNameLen);
			offset += sourceNameLen + sizeof(int);
			pulledTableName[sourceNameLen] = 0;

			int indexNameLen = 0;
			memcpy(&indexNameLen, indexRow.buffer + offset, sizeof(int));
			char* pulledIndexName = (char*)malloc(indexNameLen + 1);
			memcpy(pulledIndexName, indexRow.buffer + offset + sizeof(int), indexNameLen);
			offset += indexNameLen + sizeof(int);
			pulledIndexName[indexNameLen] = 0;
			std::string indexName = string(pulledIndexName);

			int attrNameLen = 0;
			memcpy(&attrNameLen, indexRow.buffer + offset, sizeof(int));
			char* pulledAttrName = (char*)malloc(attrNameLen + 1);
			memcpy(pulledAttrName, indexRow.buffer + offset + sizeof(int), attrNameLen);
			offset += attrNameLen;
			pulledAttrName[attrNameLen] = 0;
			std::string attrName = string(pulledAttrName);

			// Open a handle to the index
			IndexMetaData indexData;
			ret = _rbfm->openFile(pulledIndexName, indexData.fileHandle);
			RETURN_ON_ERR(ret);

			// Create a new metadata record for the index
			IndexMetaData indexMetaData;
			Attribute indexAttr;
			indexAttr.type = TypeVarChar;
			indexAttr.length = attrNameLen;
			indexAttr.name = attrName;
			indexMetaData.fileHandle = indexData.fileHandle;
			indexMetaData.attribute = indexAttr;

			// Now add this index entry to the catalog
			_catalog[tableName].indexes.insert(std::pair<std::string,IndexMetaData>(indexName, indexMetaData));

			// Advance!
			indexRid = indexRow.nextIndex;
		}
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
        ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_ATTRIBUTE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, currentRID, &attrRec);
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
		ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_ATTRIBUTE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, attributeRid, &attributeBuffer);
		RETURN_ON_ERR(ret);

		// Delete the current attribute
		++deletedAttributes;
		ret = _rbfm->deleteRecord(_catalog[SYSTEM_TABLE_ATTRIBUTE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, attributeRid);
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

RC RelationManager::findDataOffset(const void* data, const std::vector<Attribute>& recordDescriptor, const std::string& attributeName, unsigned& dataOffset)
{
	// Find where this attribute is in the list of valid attributes
	unsigned attributeIndex = 0;
	RC ret = RBFM_ScanIterator::findAttributeByName(recordDescriptor, attributeName, attributeIndex);
	RETURN_ON_ERR(ret);
	
	// Iterate through the attributes and sum up the sizes of the ones before the one we care about
	dataOffset = 0;
	for (unsigned int i = 0; i < attributeIndex; ++i)
	{
		dataOffset += Attribute::sizeInBytes(recordDescriptor[i].type, (char*)data + dataOffset);
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
	RC ret = _rbfm->insertRecord(tableData.fileHandle, tableData.recordDescriptor, data, rid);
	RETURN_ON_ERR(ret);

	// Update indices if they exist
	IndexManager* im = IndexManager::instance();
	for (std::map<std::string, IndexMetaData>::iterator it = tableData.indexes.begin(); it != tableData.indexes.end(); ++it)
	{
		// Find the offset into the tuple which has the data we care about in this index
		unsigned dataOffset = 0;
		ret = findDataOffset(data, tableData.recordDescriptor, it->second.attribute.name, dataOffset);
		RETURN_ON_ERR(ret);
		
		ret = im->insertEntry(it->second.fileHandle, it->second.attribute, (char*)data + dataOffset, rid);
		RETURN_ON_ERR(ret);
	}

	return rc::OK;
}

RC RelationManager::deleteTuples(const string &tableName)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	RC ret = _rbfm->deleteRecords(tableData.fileHandle);
	RETURN_ON_ERR(ret);

	// Update indices if they exist
	IndexManager* im = IndexManager::instance();
	for (std::map<std::string, IndexMetaData>::iterator it = tableData.indexes.begin(); it != tableData.indexes.end(); ++it)
	{
		ret = im->deleteRecords(it->second.fileHandle);
		RETURN_ON_ERR(ret);
	}

	return rc::OK;
}

RC RelationManager::deleteTuple(const string &tableName, const RID &rid)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];

	// Read the tuple in since we need it to search for the corresponding values in the index
	char oldData[PAGE_SIZE] = {0};
	RC ret = _rbfm->readRecord(tableData.fileHandle, tableData.recordDescriptor, rid, oldData);
	RETURN_ON_ERR(ret);

	// Now we can delete the actual data
	ret =  _rbfm->deleteRecord(tableData.fileHandle, tableData.recordDescriptor, rid);
	RETURN_ON_ERR(ret);

	// Update indices if they exist
	IndexManager* im = IndexManager::instance();
	for (std::map<std::string, IndexMetaData>::iterator it = tableData.indexes.begin(); it != tableData.indexes.end(); ++it)
	{
		// Find the offset into the tuple which has the data we care about in this index
		unsigned dataOffset = 0;
		ret = findDataOffset(oldData, tableData.recordDescriptor, it->second.attribute.name, dataOffset);
		RETURN_ON_ERR(ret);

		ret = im->deleteEntry(it->second.fileHandle, it->second.attribute, oldData + dataOffset, rid);
		RETURN_ON_ERR(ret);
	}

	return rc::OK;
}

RC RelationManager::updateTuple(const string &tableName, const void *data, const RID &rid)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];

	// Read the tuple in since we need it to search for the corresponding values in the index
	char oldData[PAGE_SIZE] = {0};
	RC ret = _rbfm->readRecord(tableData.fileHandle, tableData.recordDescriptor, rid, oldData);
	RETURN_ON_ERR(ret);

	// Now we can update the actual record entry
	ret = _rbfm->updateRecord(tableData.fileHandle, tableData.recordDescriptor, data, rid);
	RETURN_ON_ERR(ret);

	// Delete old index entries
	IndexManager* im = IndexManager::instance();
	for (std::map<std::string, IndexMetaData>::iterator it = tableData.indexes.begin(); it != tableData.indexes.end(); ++it)
	{
		// Find the offset into the tuple which has the data we care about in this index
		unsigned dataOffset = 0;
		ret = findDataOffset(oldData, tableData.recordDescriptor, it->second.attribute.name, dataOffset);
		RETURN_ON_ERR(ret);

		// Delete the old index entry
		ret = im->deleteEntry(it->second.fileHandle, it->second.attribute, oldData + dataOffset, rid);
		RETURN_ON_ERR(ret);
	}

	// Insert new index entries
	for (std::map<std::string, IndexMetaData>::iterator it = tableData.indexes.begin(); it != tableData.indexes.end(); ++it)
	{
		// Find the offset into the tuple which has the data we care about in this index
		unsigned dataOffset = 0;
		ret = findDataOffset(data, tableData.recordDescriptor, it->second.attribute.name, dataOffset);
		RETURN_ON_ERR(ret);

		// Delete the old index entry
		ret = im->insertEntry(it->second.fileHandle, it->second.attribute, (char*)data + dataOffset, rid);
		RETURN_ON_ERR(ret);
	}

	return rc::OK;
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

RC RM_IndexScanIterator::init(TableMetaData& _tableData, const string& indexName, const string& attributeName, const void *lowKey, const void *highKey, bool lowKeyInclusive, bool highKeyInclusive)
{
	tableData = &_tableData;
	
	// Find the attribute we care about
	unsigned attributeIndex = 0;
	RC ret = RBFM_ScanIterator::findAttributeByName(tableData->recordDescriptor, attributeName, attributeIndex);
	RETURN_ON_ERR(ret);

	// Find the index we care about
	if (tableData->indexes.find(indexName) == tableData->indexes.end())
	{
		return rc::INDEX_NOT_FOUND;
	}

	return iter.init(&(tableData->indexes[indexName].fileHandle), tableData->recordDescriptor[attributeIndex], lowKey, highKey, lowKeyInclusive, highKeyInclusive);
}

RC RelationManager::createIndex(const string &tableName, const string &attributeName)
{
	IndexManager* im = IndexManager::instance();
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];

	// Create the file that will hold our index
	const std::string indexName = getIndexName(tableName, attributeName);
	RC ret = im->createFile(indexName);
	RETURN_ON_ERR(ret);

	// Open a file handle and save it with the cached table data
	IndexMetaData& indexData = tableData.indexes[indexName];
	ret = im->openFile(indexName, indexData.fileHandle);
	RETURN_ON_ERR(ret);

	// Keep track that we have created this index system index table
	RID indexRid;
	IndexSystemRecord indexRecord(tableName, indexName, attributeName);
	indexRecord.nextIndex.pageNum = 0;
	indexRecord.nextIndex.slotNum = 0;
	ret = _rbfm->insertRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, &indexRecord, indexRid);
	RETURN_ON_ERR(ret);

	// Store the RID of this row in the in-memory catalog for easy access
	RID newIndexRid;
    RID tableRid = _catalog[tableName].rowRID;
    TableMetadataRow tableRow;
	ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, tableRid, (void*)(&tableRow));
    RETURN_ON_ERR(ret);

    // Save the new index entry to the right spot in the index table
    IndexSystemRecord prevIndexRow;
    IndexSystemRecord indexRow;
    RID prevIndexRid;
    newIndexRid = tableRow.firstIndex;
    if (newIndexRid.pageNum > 0)
    {
		while (newIndexRid.pageNum > 0)
	    {
	    	ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, newIndexRid, (void*)(&indexRow));
	    	RETURN_ON_ERR(ret);
	    	prevIndexRow = indexRow;
	    	prevIndexRid = newIndexRid;
	    	newIndexRid = indexRow.nextIndex;
	    }

	    // newIndex points to the end
	    prevIndexRow.nextIndex = indexRid;
	    ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, &prevIndexRow, prevIndexRid);
    	RETURN_ON_ERR(ret);
    }
    else // first index for this table...
    {
    	tableRow.firstIndex = indexRid;
    	ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, &tableRow, tableRid);
    	RETURN_ON_ERR(ret);
	}

	// We only want to extract the attribute for this index
	std::vector<std::string> attributeNames;
	attributeNames.push_back(attributeName);

	// Pull out the attribute data from the attribute name
	unsigned attributeIndex = 0;
	ret = RBFM_ScanIterator::findAttributeByName(tableData.recordDescriptor, attributeName, attributeIndex);
	RETURN_ON_ERR(ret);

	// Save the attribute for this index in the cached table data
	indexData.attribute = tableData.recordDescriptor[attributeIndex];
	
	RM_ScanIterator scanner;
	ret = scan(tableName, attributeName, NO_OP, NULL, attributeNames, scanner);
	RETURN_ON_ERR(ret);

	// Iterate through everything in the input table and add entries into the new index
	// Repeated insert, oh well...
	RID rid;
	char tupleBuffer[PAGE_SIZE] = {0};
	while((ret = scanner.getNextTuple(rid, tupleBuffer)) == rc::OK)
	{
		// Insert the value into our index now
		ret = im->insertEntry(indexData.fileHandle, tableData.recordDescriptor[attributeIndex], tupleBuffer, rid);
		RETURN_ON_ERR(ret);

		memset(tupleBuffer, 0, PAGE_SIZE);
	}

	return rc::OK;
}

RC RelationManager::destroyIndex(const string &tableName, const string &attributeName, bool wipeAll)
{
	std::map<std::string, TableMetaData>::iterator it = _catalog.find(tableName);
	if (it == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	// Recover the name that stores the index
	const std::string indexName = getIndexName(tableName, attributeName);
	vector<RID> indexRids;
	vector<std::string> indexFileNames;

	// Load in the row that is to be deleted
	TableMetadataRow currentRow;
    RC ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, it->second.rowRID, &currentRow);
	RETURN_ON_ERR(ret);

	// Walk the index list until we find the one that needs to be deleted
	RID prevIndexRid;
	RID indexRid;
	IndexSystemRecord prevIndexRow;
	IndexSystemRecord indexRow;
	indexRid = currentRow.firstIndex;

	bool first = true;
	bool done = false;
	indexRid = currentRow.firstIndex;
	while (indexRid.pageNum > 0 && !done)
	{
		ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, indexRid, &indexRow);
		RETURN_ON_ERR(ret);

		int offset = 0;
		int sourceNameLen = 0;
		memcpy(&sourceNameLen, indexRow.buffer + offset, sizeof(int));
		char* pulledTableName = (char*)malloc(sourceNameLen + 1);
		memcpy(pulledTableName, indexRow.buffer + offset + sizeof(int), sourceNameLen);
		offset += sourceNameLen + sizeof(int);
		pulledTableName[sourceNameLen] = 0;

		int indexNameLen = 0;
		memcpy(&indexNameLen, indexRow.buffer + offset, sizeof(int));
		char* pulledIndexName = (char*)malloc(indexNameLen + 1);
		memcpy(pulledIndexName, indexRow.buffer + offset + sizeof(int), indexNameLen);
		offset += indexNameLen + sizeof(int);
		pulledIndexName[indexNameLen] = 0;

		int attrNameLen = 0;
		memcpy(&attrNameLen, indexRow.buffer + offset, sizeof(int));
		char* pulledAttrName = (char*)malloc(attrNameLen + 1);
		memcpy(pulledAttrName, indexRow.buffer + offset + sizeof(int), attrNameLen);
		offset += attrNameLen;
		pulledAttrName[attrNameLen] = 0;

		if (!wipeAll)
		{
			if (strncmp(tableName.c_str(), pulledTableName, sourceNameLen) == 0 && strncmp(attributeName.c_str(), pulledAttrName, attrNameLen) == 0)
			{
				ret = _rbfm->deleteRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, indexRid);
				RETURN_ON_ERR(ret);

				// If we match on the first element, then update the catalog entry
				if (first)
				{
					currentRow.firstIndex = indexRow.nextIndex;
					ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_CATALOG_NAME].fileHandle, _systemTableRecordDescriptor, &currentRow, it->second.rowRID);
	    			RETURN_ON_ERR(ret);
	    			done = true;
				}
				else // otherwise, update the list internally
				{
					prevIndexRow.nextIndex = indexRow.nextIndex;
					ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, &prevIndexRow, prevIndexRid);
	    			RETURN_ON_ERR(ret);
	    			done = true;
				}
			}
		}
		else
		{
			indexRids.push_back(indexRid);
			indexFileNames.push_back(getIndexName(tableName, pulledAttrName));
		}
		
		// Advance...
		first = false;
		prevIndexRow = indexRow;
		prevIndexRid = indexRid;
		indexRid = indexRow.nextIndex;
	}

	// If we're wiping the index entirely, just drop every record for this index from the index table and then delete the file
	if (wipeAll)
	{
		for (int i = 0; i < indexRids.size(); i++)
		{
			ret = _rbfm->deleteRecord(_catalog[SYSTEM_TABLE_INDEX_NAME].fileHandle, _systemTableIndexRecordDescriptor, indexRids.at(i));
			RETURN_ON_ERR(ret);

			// Find the index's open FileHandle
			IndexManager* im = IndexManager::instance();
			std::map<std::string, IndexMetaData>::iterator indexMeta = it->second.indexes.find(indexFileNames.at(i));
			if (indexMeta != it->second.indexes.end())
			{
				// Close the file
				im->closeFile(indexMeta->second.fileHandle);
			}

			// Finally we can destroy the file
			ret = IndexManager::instance()->destroyFile(indexFileNames.at(i));
			RETURN_ON_ERR(ret);
		}
	}
	else
	{
		// After the catalog is patched, destroy the file on disk
		// Find the index's open FileHandle
		IndexManager* im = IndexManager::instance();
		std::map<std::string, IndexMetaData>::iterator indexMeta = it->second.indexes.find(indexName);
		if (indexMeta != it->second.indexes.end())
		{
			// Close the file
			im->closeFile(indexMeta->second.fileHandle);
		}
		
		// Finally we can destroy the file
		ret = IndexManager::instance()->destroyFile(indexName);
		RETURN_ON_ERR(ret);
	}

	return rc::OK;
}

RC RelationManager::destroyIndex(const string &tableName, const string &attributeName)
{
	return destroyIndex(tableName, attributeName, false);
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
	const std::string indexName = getIndexName(tableName, attributeName);

	return rm_IndexScanIterator.init(tableData, indexName, attributeName, lowKey, highKey, lowKeyInclusive, highKeyInclusive);
}

IndexSystemRecord::IndexSystemRecord(const std::string& sourceTable, const std::string& fileName, const std::string& attrName)
{
	memset(buffer, 0, sizeof(buffer));

	unsigned offset = 0;
	unsigned len = 0;
	
	len = sourceTable.length();
	memcpy(buffer + offset, &len, sizeof(len));
	offset += sizeof(len);
	memcpy(buffer + offset, sourceTable.c_str(), len);
	offset += len;

	len = fileName.length();
	memcpy(buffer + offset, &len, sizeof(len));
	offset += sizeof(len);
	memcpy(buffer + offset, fileName.c_str(), len);
	offset += len;

	len = attrName.length();
	memcpy(buffer + offset, &len, sizeof(len));
	offset += sizeof(len);
	memcpy(buffer + offset, attrName.c_str(), len);
	offset += len;
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
