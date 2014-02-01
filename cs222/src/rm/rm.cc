#include "rm.h"
#include "../util/returncodes.h"
#include <assert.h>

#define SYSTEM_TABLE_NAME "RM_SYS_TABLE.db"

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
	attr.name = "RecordDescriptorRID_page";		_systemTableRecordDescriptor.push_back(attr);
	attr.name = "RecordDescriptorRID_slot";		_systemTableRecordDescriptor.push_back(attr);
	attr.name = "NextRowRID_page";				_systemTableRecordDescriptor.push_back(attr);
	attr.name = "NextRowRID_slot";				_systemTableRecordDescriptor.push_back(attr);
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
	createTable(SYSTEM_TABLE_NAME, _systemTableRecordDescriptor);

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
	ret = _rbfm->createFile(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Save a file handle for the table
	_catalog[tableName] = TableMetaData();
	ret = _rbfm->openFile(tableName, _catalog[tableName].fileHandle);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Copy over the given record descriptor to local memory
	for (vector<Attribute>::const_iterator it = attrs.begin(); it != attrs.end(); ++it)
	{
		_catalog[tableName].recordDescriptor.push_back(*it);
	}

	RID prevLastRID = _lastTableRID;

	// Insert table metadata into the system table
	TableMetadataRow newRow;
	newRow.nextRow.pageNum = 0;
	newRow.nextRow.slotNum = 0;
	newRow.firstAttribute.pageNum = 0;
	newRow.firstAttribute.slotNum = 0;
	newRow.numAttributes = attrs.size();
	newRow.owner = ((tableName == SYSTEM_TABLE_NAME) ? TableOwnerSystem : TableOwnerUser); // TODO: Better way of doing this

	int tableNameLen = tableName.size();
	if (tableNameLen >= MAX_TABLENAME_SIZE)
	{
		return rc::TABLE_NAME_TOO_LONG;
	}
	
	memcpy(newRow.tableName, &tableNameLen, sizeof(int));
	memcpy(newRow.tableName + sizeof(int), tableName.c_str(), tableNameLen);

	ret = _rbfm->insertRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableRecordDescriptor, &newRow, _lastTableRID);
	if (ret != rc::OK)
	{
		return ret;
	}

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
		ret = _rbfm->insertRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, &attrRec, attributeRID);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

	// Update the row data with the pointer to its first attribute
	newRow.firstAttribute.pageNum = attributeRID.pageNum;
	newRow.firstAttribute.slotNum = attributeRID.slotNum;
	ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableRecordDescriptor, &newRow, _lastTableRID);

	// If there was a previous row, we need to update its next pointer
	if (prevLastRID.pageNum > 0)
	{
		TableMetadataRow prevRow;
		ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableRecordDescriptor, prevLastRID, &prevRow);
		if (ret != rc::OK)
		{
			return ret;
		}

		prevRow.nextRow.pageNum = _lastTableRID.pageNum;
		prevRow.nextRow.slotNum = _lastTableRID.slotNum;

		ret = _rbfm->updateRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableRecordDescriptor, &prevRow, prevLastRID);
		if (ret != rc::OK)
		{
			return ret;
		}
	}

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
	ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableRecordDescriptor, currentRID, &currentRow);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Since we already know everything about the system table, move onto the next pointer for the actual user tables
	currentRID.pageNum = currentRow.nextRow.pageNum;
	currentRID.slotNum = currentRow.nextRow.slotNum;

	while (currentRID.pageNum > 0)
	{
		// Read in the next row metadata
		memset(&currentRow, 0, sizeof(currentRow));
		ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableRecordDescriptor, currentRID, &currentRow);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Read in the column information for this row
		ret = loadTableColumnMetadata(currentRow.numAttributes, currentRow.firstAttribute, _catalog[std::string(currentRow.tableName)].recordDescriptor);
		if (ret != rc::OK)
		{
			return ret;
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
		ret = _rbfm->readRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableAttributeRecordDescriptor, firstAttributeRID, &attrRec);
		if (ret != rc::OK)
		{
			return ret;
		}

		// Copy over the data into a regular attribute and add this attribute to the recordDescriptor 
		Attribute attr;
		attr.length = attrRec.length;
		attr.type = attrRec.type;
		attr.name = std::string(attrRec.name);
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
	if (ret != rc::OK)
	{
		return ret;
	}

	ret = _rbfm->destroyFile(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	// TODO: Delete the row from the system table
	
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

// Extra credit
RC RelationManager::dropAttribute(const string &tableName, const string &attributeName)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];

	// TODO: Write me
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

// Extra credit
RC RelationManager::addAttribute(const string &tableName, const Attribute &attr)
{
	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];

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
