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
	Attribute attr;

	attr.type = TypeInt;
	attr.length = sizeof(int);

	attr.name = "Owner";
	_systemTableRecordDescriptor.push_back(attr);

	attr.name = "NumAttributes";
	_systemTableRecordDescriptor.push_back(attr);

	attr.name = "RecordDescriptorRID_page";
	_systemTableRecordDescriptor.push_back(attr);

	attr.name = "RecordDescriptorRID_slot";
	_systemTableRecordDescriptor.push_back(attr);

	attr.name = "NextRowRID_page";
	_systemTableRecordDescriptor.push_back(attr);

	attr.name = "NextRowRID_slot";
	_systemTableRecordDescriptor.push_back(attr);

	attr.name = "TableName";
	attr.type = TypeVarChar;
	attr.length = MAX_TABLENAME_SIZE;
	_systemTableRecordDescriptor.push_back(attr);

	createTable(SYSTEM_TABLE_NAME, _systemTableRecordDescriptor);
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
	if (_catalog.find(tableName) != _catalog.end())
	{
		return rc::TABLE_ALREADY_CREATED;
	}

	ret = _rbfm->createFile(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	// Save a file handle to the table
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

	// Insert table metadata into the system table
	TableMetadataRow tableMetadataRow;
	memset(&tableMetadataRow, 0, sizeof(tableMetadataRow));
	tableMetadataRow.next.pageNum = 0;
	tableMetadataRow.next.slotNum = 0;
	tableMetadataRow.numAttributes = attrs.size();
	tableMetadataRow.owner = tableName == SYSTEM_TABLE_NAME ? TableOwnerSystem : TableOwnerUser;

	int tableNameLen = tableName.size();
	if (tableNameLen >= MAX_TABLENAME_SIZE)
	{
		return rc::TABLE_NAME_TOO_LONG;
	}
	
	memcpy(tableMetadataRow.tableName, &tableNameLen, sizeof(int));
	memcpy(tableMetadataRow.tableName + sizeof(int), tableName.c_str(), tableNameLen);

	ret = _rbfm->insertRecord(_catalog[SYSTEM_TABLE_NAME].fileHandle, _systemTableRecordDescriptor, &tableMetadataRow, _lastTableRID);
	if (ret != rc::OK)
	{
		return ret;
	}

	// TODO:
	// If there was a metadata row already, update the next pointer for it
	// TODO:

    return rc::OK;
}

RC RelationManager::loadTable(const std::string& tableName)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
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
	
	return rc::OK;
}

RC RelationManager::getAttributes(const string &tableName, vector<Attribute> &attrs)
{
	RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return rc::OK;
}

RC RelationManager::insertTuple(const string &tableName, const void *data, RID &rid)
{
    RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->insertRecord(tableData.fileHandle, recordDescriptor, data, rid);
}

RC RelationManager::deleteTuples(const string &tableName)
{
    RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	TableMetaData& tableData = _catalog[tableName];
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->deleteRecords(tableData.fileHandle);
}

RC RelationManager::deleteTuple(const string &tableName, const RID &rid)
{
    RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->deleteRecord(tableData.fileHandle, recordDescriptor, rid);
}

RC RelationManager::updateTuple(const string &tableName, const void *data, const RID &rid)
{
    RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->updateRecord(tableData.fileHandle, recordDescriptor, data, rid);
}

RC RelationManager::readTuple(const string &tableName, const RID &rid, void *data)
{
    RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->readRecord(tableData.fileHandle, recordDescriptor, rid, data);
}

RC RelationManager::readAttribute(const string &tableName, const RID &rid, const string &attributeName, void *data)
{
    RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->readAttribute(tableData.fileHandle, recordDescriptor, rid, attributeName, data);
}

RC RelationManager::reorganizePage(const string &tableName, const unsigned pageNumber)
{
    RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->reorganizePage(tableData.fileHandle, recordDescriptor, pageNumber);
}

RC RelationManager::scan(const string &tableName,
      const string &conditionAttribute,
      const CompOp compOp,                  
      const void *value,                    
      const vector<string> &attributeNames,
      RM_ScanIterator &rm_ScanIterator)
{
	RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->scan(
		tableData.fileHandle, 
		recordDescriptor, 
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
	RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	// TODO: Write me
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

// Extra credit
RC RelationManager::addAttribute(const string &tableName, const Attribute &attr)
{
	RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	// TODO: Write me
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

// Extra credit
RC RelationManager::reorganizeTable(const string &tableName)
{
	RC ret = loadTable(tableName);
	if (ret != rc::OK)
	{
		return ret;
	}

	if (_catalog.find(tableName) == _catalog.end())
	{
		return rc::TABLE_NOT_FOUND;
	}

	std::vector<Attribute> recordDescriptor;
	TableMetaData& tableData = _catalog[tableName];
	ret = loadColumnAttributes(tableData.fileHandle, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->reorganizeFile(tableData.fileHandle, recordDescriptor);
}

RC RelationManager::loadColumnAttributes(FileHandle& fileHandle, std::vector<Attribute>& recordDescriptor)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}
