
#include "qe.h"

Filter::Filter(Iterator* input, const Condition &condition) {
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