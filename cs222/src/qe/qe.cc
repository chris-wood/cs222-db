#include "qe.h"
#include <assert.h>

bool Condition::compare(void* thatData) const
{
	return RBFM_ScanIterator::compareData(rhsValue.type, op, thatData, rhsValue.data);
}

Filter::Filter(Iterator* input, const Condition &condition)
: _input(input),
_condition(condition)
{
	// TODO: What does this mean for a Filter if this is true?
	assert(_condition.bRhsIsAttr == false);

	/*
	RC ret = Condition::splitAttr(_condition.lhsAttr, _lhsRel, _lhsAttr);
	assert(ret == rc::OK);
	*/

	getAttributes(_lhsAttributes);
	RC ret = RBFM_ScanIterator::findAttributeByName(_lhsAttributes, _condition.lhsAttr, _lhsAttrIndex);
	assert(ret == rc::OK);
}

void Filter::getAttributes(vector<Attribute> &attrs) const
{
	return _input->getAttributes(attrs);
}

RC Filter::getDataOffset(const vector<Attribute>& attrs, const string& attrName, const void* data, unsigned& dataOffset)
{
	dataOffset = 0;
	for (unsigned int i = 0; i < _lhsAttrIndex; ++i)
	{
		Attribute attr = attrs[i];
		dataOffset += Attribute::sizeInBytes(attr.type, (char*)data + dataOffset);
	}

	return rc::OK;
}

RC Condition::splitAttr(const string& input, string& rel, string& attr)
{
	RC ret = rc::OK;
	
	const size_t length = input.length();
	const size_t splitPos = input.find_first_of('.');

	if (splitPos == string::npos)
	{
		return rc::ATTRIBUTE_STRING_NO_DOT_SEPARATOR;
	}

	if (splitPos == 0)
	{
		return rc::ATTRIBUTE_STRING_NO_REL_DATA;
	}

	if (splitPos + 1 >= length)
	{
		return rc::ATTRIBUTE_STRING_NO_ATTR_DATA;
	}

	rel = string(input, 0, splitPos);
	attr = string(input, splitPos + 1, length - splitPos - 1);

	return rc::OK;
}

Project::Project(Iterator* input, const vector<string> &attrNames) 
{
	_itr = input;
	_projectAttributeNames = attrNames;
	_itr->getAttributes(_itrAttributes);

	// Build and save projection attributes
	for (int i = 0; i < _projectAttributeNames.size(); i++)
	{
		for (int j = 0; j < _itrAttributes.size(); j++)
		{
			if (_projectAttributeNames[i] == _itrAttributes[j].name)
			{
				Attribute attr;
				attr.name = _itrAttributes[j].name;
				attr.type = _itrAttributes[j].type;
				attr.length = _itrAttributes[j].length;
				_projectAttributes.push_back(attr);
			}
		}
	}

	// Save the entry record size
	_entrySize = 0;
	for (int i = 0; i < _itrAttributes.size(); i++)
	{
		_entrySize += _itrAttributes[i].length;
	} 
	_entryBuffer = malloc(_entrySize);
}

Project::~Project()
{
	if (_entryBuffer != NULL)
	{
		free(_entryBuffer);
	}	
}

void Project::getAttributes(vector<Attribute> &attrs) const
{
	attrs.clear(); // ensure we don't add on more than what's expected
	for (int i = 0; i < _projectAttributes.size(); i++)
	{
		attrs.push_back(_projectAttributes[i]);
	}
}

RC Project::getNextTuple(void *data)
{
	unsigned dataSize = 0;
	vector<unsigned> offsets;
	vector<unsigned> sizes;
	vector<unsigned> saveIndices;

	// Reset the buffer to read in fresh data
	memset(_entryBuffer, 0, _entrySize);
	RC ret = _itr->getNextTuple(_entryBuffer);
	if (ret != rc::OK)
	{
		cout << rc::rcToString(ret) << endl;
		return ret;
	}

	// Now filter out only the projections we want
	for (int i = 0; i < _itrAttributes.size(); i++)
	{

		// TODO: refactor to use Attribute::sizeInBytes(attr.type, (char*)data + dataOffset);

		Attribute attr = _itrAttributes[i];
		switch (attr.type)
		{
			case TypeInt:
			case TypeReal:
				dataSize += 4;

				offsets.push_back(dataSize);
				sizes.push_back(4);
				break;
			case TypeVarChar:
				dataSize += sizeof(unsigned);
				int len = 0;
				memcpy(&len, (char*)_entryBuffer + dataSize, sizeof(unsigned));

				offsets.push_back(dataSize);
				sizes.push_back(len);
		}

		// Determine if we match or not, and if so, save the info to re-build the attribute
		for (int j = 0; j < _projectAttributeNames.size(); j++)
		{
			if (attr.name == _projectAttributeNames[j])
			{
				saveIndices.push_back(i);
				break;
			}
		}
	}

	// Copy over the result
	unsigned offset = 0;
	for (int i = 0; i < saveIndices.size(); i++)
	{
		memcpy((char*)data + offset, (char*)_entryBuffer + offsets[saveIndices[i]], sizes[saveIndices[i]]);
	}

	return rc::OK;
}

RC Filter::getNextTuple(void *data)
{
	RC ret = rc::UNKNOWN_FAILURE;
	
	// Iterate until we find a valid tuple, or we have no more items to iterate through
	while (ret != rc::OK && ret != QE_EOF)
	{
		// Get the next tuple from the iterator
		ret = _input->getNextTuple(data);
		if (ret == QE_EOF)
		{
			break;
		}

		// Find where the attribute we care about is in the returned tuple
		unsigned dataOffset = 0;
		ret = getDataOffset(_lhsAttributes, _condition.lhsAttr, data, dataOffset);
		RETURN_ON_ERR(ret);

		// Check if this tuple matches the condition we care about
		if (_condition.compare((char*)data + dataOffset))
		{
			ret = rc::OK;
		}
		else
		{
			// Fail temporarily so we can get another tuple when we loop
			ret = rc::TUPLE_COMPARE_CONDITION_FAILED;
		}
	}

	return ret;
}

