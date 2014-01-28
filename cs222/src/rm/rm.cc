#include "rm.h"
#include "../util/returncodes.h"

RelationManager* RelationManager::_rm = 0;

RelationManager* RelationManager::instance()
{
    if(!_rm)
        _rm = new RelationManager();

    return _rm;
}

RelationManager::RelationManager()
{
	_rbfm = RecordBasedFileManager::instance();
}

RelationManager::~RelationManager()
{
	_rm = NULL;
}

RC RelationManager::createTable(const string &tableName, const vector<Attribute> &attrs)
{
    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RelationManager::deleteTable(const string &tableName)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

RC RelationManager::getAttributes(const string &tableName, vector<Attribute> &attrs)
{
     RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	return loadColumnAttributes(table, recordDescriptor);
}

RC RelationManager::insertTuple(const string &tableName, const void *data, RID &rid)
{
    RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->insertRecord(table, recordDescriptor, data, rid);
}

RC RelationManager::deleteTuples(const string &tableName)
{
    RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->deleteRecords(table);
}

RC RelationManager::deleteTuple(const string &tableName, const RID &rid)
{
    RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->deleteRecord(table, recordDescriptor, rid);
}

RC RelationManager::updateTuple(const string &tableName, const void *data, const RID &rid)
{
    RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->updateRecord(table, recordDescriptor, data, rid);
}

RC RelationManager::readTuple(const string &tableName, const RID &rid, void *data)
{
    RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->readRecord(table, recordDescriptor, rid, data);
}

RC RelationManager::readAttribute(const string &tableName, const RID &rid, const string &attributeName, void *data)
{
    RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->readAttribute(table, recordDescriptor, rid, attributeName, data);
}

RC RelationManager::reorganizePage(const string &tableName, const unsigned pageNumber)
{
    RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	// TODO: What does the recordDescriptor do?
	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->reorganizePage(table, recordDescriptor, pageNumber);
}

RC RelationManager::scan(const string &tableName,
      const string &conditionAttribute,
      const CompOp compOp,                  
      const void *value,                    
      const vector<string> &attributeNames,
      RM_ScanIterator &rm_ScanIterator)
{
	RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

	return _rbfm->scan(
		table, 
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
	RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

// Extra credit
RC RelationManager::addAttribute(const string &tableName, const Attribute &attr)
{
	RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	std::vector<Attribute> recordDescriptor;
	ret = loadColumnAttributes(table, recordDescriptor);
	if (ret != rc::OK)
	{
		return ret;
	}

    return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

// Extra credit
RC RelationManager::reorganizeTable(const string &tableName)
{
	RC ret = rc::OK;
    FileHandle& table = loadTable(tableName, ret);
	if (ret != rc::OK)
	{
		return ret;
	}

	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}

FileHandle& RelationManager::loadTable(const std::string& tableName, RC& ret)
{
	if (_openFiles.find(tableName) == _openFiles.end())
	{
		ret = _rbfm->openFile(tableName, _openFiles[tableName]);
	}

	return _openFiles[tableName];
}

RC RelationManager::loadColumnAttributes(FileHandle& fileHandle, std::vector<Attribute>& recordDescriptor)
{
	return rc::FEATURE_NOT_YET_IMPLEMENTED;
}
