#include "qe.h"
#include <assert.h>

bool Condition::compare(AttrType type, const void* left, const void* right) const
{
	return RBFM_ScanIterator::compareData(type, op, left, right);
}

bool Condition::compare(const void* thatData) const
{
	return RBFM_ScanIterator::compareData(rhsValue.type, op, thatData, rhsValue.data);
}

Filter::Filter(Iterator* input, const Condition &condition)
: _input(input),
_condition(condition)
{
	// TODO: What does this mean for a Filter if this is true?
	assert(_condition.bRhsIsAttr == false);

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
	for (unsigned i = 0; i < _projectAttributeNames.size(); i++)
	{
		for (unsigned j = 0; j < _itrAttributes.size(); j++)
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
	for (unsigned i = 0; i < _itrAttributes.size(); i++)
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
	for (unsigned i = 0; i < _projectAttributes.size(); i++)
	{
		attrs.push_back(_projectAttributes[i]);
	}
}

RC Project::getNextTuple(void *data)
{
	unsigned len = 0;
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
	for (unsigned i = 0; i < _itrAttributes.size(); i++)
	{
		Attribute attr = _itrAttributes[i];

		offsets.push_back(dataSize);
		len = Attribute::sizeInBytes(attr.type, (char*)_entryBuffer + dataSize);
		dataSize += len;
		sizes.push_back(len);

		// Determine if we match or not, and if so, save the info to re-build the attribute
		for (unsigned j = 0; j < _projectAttributeNames.size(); j++)
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
	for (unsigned i = 0; i < saveIndices.size(); i++)
	{
		const unsigned index = saveIndices[i];
		const unsigned length = sizes[index];
		memcpy((char*)data + offset, (char*)_entryBuffer + offsets[index], length);
		offset += length;
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

JoinerData::JoinerData(Iterator* in, const string& attributeName)
: iter(in), attributeIndex(0), prevStatus(rc::ITERATOR_NEVER_CALLED)
{
	assert(iter != NULL);
	iter->getAttributes(attributes);

	RC ret = RBFM_ScanIterator::findAttributeByName(attributes, attributeName, attributeIndex);
	assert(ret == rc::OK);
}

Joiner::Joiner(Iterator *leftIn, Iterator *rightIn, const Condition &condition, const unsigned numPages)
: _outer(leftIn, condition.lhsAttr), _inner(rightIn, condition.rhsAttr), _condition(condition), _numPages(numPages)
{
	// TODO: Not sure what to do if this is false
	assert(_condition.bRhsIsAttr);

	_pageBuffer = (char*)malloc(PAGE_SIZE * _numPages);

	assert(_pageBuffer != NULL);

	_attrType = _outer.attributes[_outer.attributeIndex].type;
	assert(_attrType == _inner.attributes[_inner.attributeIndex].type);
}

Joiner::~Joiner()
{
	free(_pageBuffer);
}

RC Joiner::copyoutData(const void* dataIn, void* dataOut, const vector<Attribute>& attributes)
{
	unsigned dataOffset = 0;
	unsigned len = 0;

	for (vector<Attribute>::const_iterator it = attributes.begin(); it != attributes.end(); ++it)
	{
		len = Attribute::sizeInBytes(it->type, (char*)dataIn + dataOffset);
		memcpy((char*)dataOut + dataOffset, (char*)dataIn + dataOffset, len);
		dataOffset += len;
	}

	return rc::OK;
}

RC Joiner::comapreData(const Condition& condition, const void* dataLeft, const void* dataRight, const vector<Attribute>& attributesLeft, const vector<Attribute>& attributesRight, unsigned attributeIndexLeft, unsigned attributeIndexRight)
{
	RC ret = rc::OK;
	const char* compareLeft = (const char*)dataLeft;
	const char* compareRight = (const char*)dataRight;
	
	for (unsigned int i = 0; i < attributeIndexLeft; ++i)
	{
		compareLeft += Attribute::sizeInBytes(attributesLeft[i].type, compareLeft);
	}

	for (unsigned int i = 0; i < attributeIndexRight; ++i)
	{
		compareRight += Attribute::sizeInBytes(attributesRight[i].type, compareRight);
	}

	return condition.compare(attributesLeft[attributeIndexLeft].type, compareLeft, compareRight);
}

RC Joiner::getNextTuple(void* data)
{
	// If both our loops have ended, we have nothing else to do
	if (_outer.prevStatus == QE_EOF && _inner.prevStatus == QE_EOF)
	{
		return QE_EOF;
	}

	// A single buffer page for each iterator
	assert(_numPages >= 2);
	char* outerPage = getPage(0);
	char* innerPage = getPage(1);
	RC ret = rc::OK;

	// Take care of initial loading of data
	if (_outer.prevStatus == rc::ITERATOR_NEVER_CALLED)
	{
		//std::cout << "Initializing outer loop" << std::endl;
		ret = _outer.prevStatus = _outer.iter->getNextTuple(outerPage);
		RETURN_ON_ERR(ret);
	}

	if (_inner.prevStatus == rc::ITERATOR_NEVER_CALLED)
	{
		//std::cout << "Initializing inner loop" << std::endl;
		ret = _inner.prevStatus = _inner.iter->getNextTuple(innerPage);
		RETURN_ON_ERR(ret);
	}

	// Nested loops JOIN
	while (_outer.prevStatus != QE_EOF)
	{
		while (_inner.prevStatus != QE_EOF)
		{
			// Compare the two values to see if this is a valid item to return
			if (_condition.compare(_attrType, outerPage, innerPage))
			{
				//std::cout << "FOUND MATCH!" << std::endl;
				// TODO: Are we just assuming we use the outer attributes?
				return copyoutData(outerPage, data, _outer.attributes);
			}
			else
			{
				//std::cout << " ++inner" << std::endl;

				// We need to advance to the next inner value to compare it
				ret = _inner.prevStatus = _inner.iter->getNextTuple(innerPage);
			}
		}

		//std::cout << "++outer" << std::endl;

		// We have reached the end of the inner loop, reset it
		ret = resetInner();
		RETURN_ON_ERR(ret);

		// Get the first element of the inner loop again
		ret = _inner.prevStatus = _inner.iter->getNextTuple(innerPage);
		RETURN_ON_ERR(ret);

		// Now advance the outer loop by one
		ret = _outer.prevStatus = _outer.iter->getNextTuple(outerPage);
	}

	// If we have reached the end of both iterators, we are done
	return QE_EOF;
}

void Joiner::getAttributes(vector<Attribute> &attrs) const
{
	// TODO: Not sure what to return here?
	_outer.iter->getAttributes(attrs);
}

RC NLJoin::resetInner()
{
	static_cast<TableScan*>(_inner.iter)->setIterator();
	return rc::OK;
}

RC INLJoin::resetInner()
{
	// TODO: Do we ever need to remember the values from the iterator initialization before?
	static_cast<IndexScan*>(_inner.iter)->setIterator(NULL, NULL, true, true);
	return rc::OK;
}
