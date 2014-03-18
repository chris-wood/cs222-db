#include "qe.h"
#include <assert.h>
#include <sstream>
#include <limits>

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
	getAttributes(_lhsAttributes);
	RC ret = RBFM_ScanIterator::findAttributeByName(_lhsAttributes, _condition.lhsAttr, _lhsAttrIndex);
	assert(ret == rc::OK);

	if( _condition.bRhsIsAttr)
	{
		ret = RBFM_ScanIterator::findAttributeByName(_lhsAttributes, _condition.rhsAttr, _rhsAttrIndex);
		assert(ret == rc::OK);
		_condition.rhsValue.type = _lhsAttributes.at(_rhsAttrIndex).type;
	}
}

void Filter::getAttributes(vector<Attribute> &attrs) const
{
	return _input->getAttributes(attrs);
}

RC Filter::getDataOffset(const vector<Attribute>& attrs, unsigned attrIndex, const void* data, unsigned& dataOffset)
{
	dataOffset = 0;
	for (unsigned int i = 0; i < attrIndex; ++i)
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
		unsigned lhsDataOffset = 0;
		ret = getDataOffset(_lhsAttributes, _lhsAttrIndex, data, lhsDataOffset);
		RETURN_ON_ERR(ret);

		// If the RHS is an attribute, load in the attribute's data
		if (_condition.bRhsIsAttr)
		{
			unsigned rhsDataOffset = 0;
			ret = getDataOffset(_lhsAttributes, _rhsAttrIndex, data, rhsDataOffset);
			_condition.rhsValue.data = (char*)data + rhsDataOffset;
		}

		// Check if this tuple matches the condition we care about
		if (_condition.compare((char*)data + lhsDataOffset))
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
: iter(in), attributeIndex(0), status(rc::ITERATOR_NEVER_CALLED)
{
	assert(iter != NULL);
	iter->getAttributes(attributes);

	RC ret = RBFM_ScanIterator::findAttributeByName(attributes, attributeName, attributeIndex);
	assert(ret == rc::OK);
}

RC JoinerData::advance(void* data)
{
	status = iter->getNextTuple(data);

	// Calculate the offset of the attribute we are using in the input tuple
	attributeOffset = 0;
	for (unsigned int i = 0; i < attributeIndex; ++i)
	{
		attributeOffset += Attribute::sizeInBytes(attributes[i].type, (char*)data + attributeOffset);
	}

	return status;
}

Joiner::Joiner(Iterator *leftIn, Iterator *rightIn, const Condition &condition, const unsigned /*numPages*/)
: _outer(leftIn, condition.lhsAttr), _inner(rightIn, condition.rhsAttr), _condition(condition), _numPages(2)
{
	_pageBuffer = (char*)malloc(PAGE_SIZE * _numPages);

	assert(_pageBuffer != NULL);

	_attrType = _outer.attributes[_outer.attributeIndex].type;
	assert(_attrType == _inner.attributes[_inner.attributeIndex].type);
}

Joiner::~Joiner()
{
	free(_pageBuffer);
}

RC Joiner::copyoutData(void* dataOut, const void* dataOuter, const void* dataInner)
{
	unsigned dataOutOffset = 0;
	unsigned dataInOffset = 0;
	unsigned len = 0;

	// first copy out the outer attributes
	for (vector<Attribute>::const_iterator it = _outer.attributes.begin(); it != _outer.attributes.end(); ++it)
	{
		len = Attribute::sizeInBytes(it->type, (char*)dataOuter + dataInOffset);
		memcpy((char*)dataOut + dataOutOffset, (char*)dataOuter + dataInOffset, len);
		dataOutOffset += len;
		dataInOffset += len;
	}

	// next copy the inner attributes
	dataInOffset = 0;
	for (vector<Attribute>::const_iterator it = _inner.attributes.begin(); it != _inner.attributes.end(); ++it)
	{
		len = Attribute::sizeInBytes(it->type, (char*)dataInner + dataInOffset);
		memcpy((char*)dataOut + dataOutOffset, (char*)dataInner + dataInOffset, len);
		dataOutOffset += len;
		dataInOffset += len;
	}

	return rc::OK;
}

RC Joiner::compareData(const Condition& condition, AttrType attrType, const void* dataLeft, const void* dataRight)
{
	return condition.compare(attrType, dataLeft, dataRight);
}

RC Joiner::getNextTuple(void* data)
{
	// If both our loops have ended, we have nothing else to do
	if (_outer.status == QE_EOF && _inner.status == QE_EOF)
	{
		return QE_EOF;
	}

	// A single buffer page for each iterator
	assert(_numPages >= 2);
	char* outerPage = getPage(0);
	char* innerPage = getPage(1);
	RC ret = rc::OK;

	// Take care of initial loading of the outer buffer
	if (_outer.status == rc::ITERATOR_NEVER_CALLED)
	{
		ret = _outer.advance(outerPage);
		RETURN_ON_ERR(ret);
	}

	// We're always advancing the inner buffer
	ret = _inner.advance(innerPage);

	// Nested loops JOIN
	while (_outer.status != QE_EOF)
	{
		while (_inner.status != QE_EOF)
		{
			// Compare the two values to see if this is a valid item to return
			if (compareData(_condition, _attrType, outerPage + _outer.attributeOffset, innerPage + _inner.attributeOffset))
			{
				return copyoutData(data, outerPage, innerPage);
			}
			else
			{
				// We need to advance to the next inner value to compare it
				ret = _inner.advance(innerPage);
			}
		}

		// We have reached the end of the inner loop, reset it
		ret = resetInner();
		RETURN_ON_ERR(ret);

		// Get the first element of the inner loop again
		ret = _inner.advance(innerPage);
		RETURN_ON_ERR(ret);

		// Now advance the outer loop by one and we'll restart the next inner loop iteration
		ret = _outer.advance(outerPage);
	}

	// If we have reached the end of both iterators, we are done
	return QE_EOF;
}

void Joiner::getAttributes(vector<Attribute> &attrs) const
{
	attrs.clear();

	// Concat both attributes together
	for (vector<Attribute>::const_iterator it = _outer.attributes.begin(); it != _outer.attributes.end(); ++it)
	{
		attrs.push_back(*it);
	}

	for (vector<Attribute>::const_iterator it = _inner.attributes.begin(); it != _inner.attributes.end(); ++it)
	{
		attrs.push_back(*it);
	}
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

Aggregate::Aggregate(Iterator* input, Attribute aggAttr, AggregateOp op)
: _input(input), _aggrigateAttribute(aggAttr), _operation(op), _hasGroup(false)
{
	_input->getAttributes(_attributes);

	RC ret = RBFM_ScanIterator::findAttributeByName(_attributes, _aggrigateAttribute.name, _aggrigateAttributeIndex);
	assert(ret == rc::OK);

	_totalAggregate.init(_attributes[_aggrigateAttributeIndex].type, _operation);
}

Aggregate::Aggregate(Iterator* input, Attribute aggAttr, Attribute gAttr, AggregateOp op)
: _input(input), _aggrigateAttribute(aggAttr), _groupAttribute(gAttr), _operation(op), _hasGroup(true)
{
	_input->getAttributes(_attributes);

	RC ret = RBFM_ScanIterator::findAttributeByName(_attributes, _aggrigateAttribute.name, _aggrigateAttributeIndex);
	assert(ret == rc::OK);

	ret = RBFM_ScanIterator::findAttributeByName(_attributes, _groupAttribute.name, _groupAttributeIndex);
	assert(ret == rc::OK);

	_totalAggregate.init(_attributes[_aggrigateAttributeIndex].type, _operation);
	_groupingAggregate.init(_attributes[_groupAttributeIndex].type, _operation);

	// Scan through the entire table and populate our hash map with grouped aggregate data
	ret = rc::OK;
	while ((ret = getNextSingleTuple()) == rc::OK)
	{
	}

	assert(ret == QE_EOF);

	// Intialize our iterators
	_realGroupingIterator = _realGrouping.begin();
	_intGroupingIterator = _intGrouping.begin();
}

void AggregateData::init(AttrType type, AggregateOp op)
{
	_count = 0;
	_readAsInt = (type == TypeInt);

	// In most cases we write out the same format we read in
	_writeAsInt = _readAsInt;
	switch (op)
	{
	case COUNT: // counts are always written out as integers
		_writeAsInt = true;
		break;

	case AVG: // averages are always writen out as reals
		_writeAsInt = false;
		break;
	}

	// Determine initial values based on what type of operation this is
	switch (op)
	{
	default:
	case SUM:
	case AVG:
	case COUNT:
		_realValue = 0.0f;
		_intValue = 0;
		break;

	case MIN:
		_realValue = std::numeric_limits<float>::max();
		_intValue  = std::numeric_limits<int>::max();
		break;

	case MAX:
		_realValue = std::numeric_limits<float>::min();
		_intValue = std::numeric_limits<int>::min();
		break;
	}
}

void AggregateData::append(AggregateOp op, float realData, int intData)
{
	++_count;

	switch (op)
	{
	case MIN:
		_realValue = (realData < _realValue) ? realData : _realValue;
		_intValue = (intData  < _intValue) ? intData : _intValue;
		break;

	case MAX:
		_realValue = (realData > _realValue) ? realData : _realValue;
		_intValue = (intData  > _intValue) ? intData : _intValue;
		break;

	case SUM:
		_realValue += realData;
		_intValue += intData;
		break;

	case AVG:
		_realValue = ((_realValue * (_count - 1)) + realData) / _count;
		_intValue = (int)(_realValue);
		break;

	default:
	case COUNT:
		_realValue = (float)_count;
		_intValue = _count;
		break;
	}
}

void AggregateData::write(void* dest) const
{
	if (_writeAsInt)
	{
		memcpy(dest, &_intValue, sizeof(_intValue));
	}
	else
	{
		memcpy(dest, &_realValue, sizeof(_realValue));
	}
}

void AggregateData::read(const void* data, unsigned attributeOffset, float& f, int& i)
{
	if (_readAsInt)
	{
		i  = *(int*)((char*)data+ attributeOffset);
		f = (float)i;
	}
	else
	{
		f = *(float*)((char*)data + attributeOffset);
		i = (int)f;
	}
}

template <typename T>
AggregateData* Aggregate::getGroupedAggregate(T value, std::map<T, AggregateData>& groupMap)
{
	typename std::map<T, AggregateData>::iterator it = groupMap.find(value);
	if (it == groupMap.end())
	{
		// We need to initialize this grouping fresh
		AggregateData newGrouping;
		newGrouping.init(_attributes[_groupAttributeIndex].type, _operation);

		// And insert it into the group
		groupMap[value] = newGrouping;
		it = groupMap.find(value);
	}
	// else we can just return the existing grouping

	assert(it != groupMap.end());
	return &(it->second);
}

RC Aggregate::getNextSingleTuple()
{
	RC ret = rc::OK;
	memset(_buffer, 0, sizeof(_buffer));

	// Read in the next tuple into our buffer
	ret = _input->getNextTuple(_buffer);
	RETURN_ON_ERR(ret);

	// Find the offset of the attribute we want
	unsigned attributeOffset = 0;
	for (unsigned int i = 0; i < _aggrigateAttributeIndex; ++i)
	{
		attributeOffset += Attribute::sizeInBytes(_attributes[i].type, _buffer + attributeOffset);
	}

	// Read the value the iterator provided
	float realValue;
	int intValue;
	_totalAggregate.read(_buffer, attributeOffset, realValue, intValue);

	// Append the value into our current total aggregation
	_totalAggregate.append(_operation, realValue, intValue);

	// If we are using grouped aggregates, determine which grouping this goes into
	if (_hasGroup)
	{
		AggregateData* aggregate = NULL;
		float groupingRealValue;
		int groupingIntValue;

		// Find the offset of the attribute we want
		attributeOffset = 0;
		for (unsigned int i = 0; i < _groupAttributeIndex; ++i)
		{
			attributeOffset += Attribute::sizeInBytes(_attributes[i].type, _buffer + attributeOffset);
		}

		_groupingAggregate.read(_buffer, attributeOffset, groupingRealValue, groupingIntValue);

		// Determine which source mapping we're using based on the GROUPING values
		if (_groupingAggregate._readAsInt)
		{
			aggregate = getGroupedAggregate<int>(groupingIntValue, _intGrouping);
			memcpy(aggregate->_groupingData, &groupingIntValue, sizeof(groupingIntValue));
		}
		else
		{
			aggregate = getGroupedAggregate<float>(groupingRealValue, _realGrouping);
			memcpy(aggregate->_groupingData, &groupingRealValue, sizeof(groupingRealValue));
		}

		// Apppend the actual AGGREGATION data to the appropriate bucket
		aggregate->append(_operation, realValue, intValue);
	}

	return rc::OK;
}

template <typename T>
RC Aggregate::getNextGroup(void* data, std::map<T, AggregateData>& groupMap, typename std::map<T, AggregateData>::const_iterator& groupIter)
{
	// Have we run out of groups to return?
	if (groupIter == groupMap.end())
	{
		return QE_EOF;
	}

	void* groupingValue = (void*)groupIter->second._groupingData;

	// Output the GROUPING value
	memcpy(data, groupingValue, 4);

	// Output the AGGREAGTE value
	groupIter->second.write((char*)data + 4);

	// Advance our iterator and we're done
	++groupIter;

	return rc::OK;
}

RC Aggregate::getNextTuple(void* data)
{
	RC ret = QE_EOF;
	if (!_hasGroup)
	{
		// Without grouping, we simply get the next tuple and output the current total aggregate data
		ret = getNextSingleTuple();
		if (ret == rc::OK)
			_totalAggregate.write(data);
	}
	else
	{
		// With grouping, we must iterate through each of the grouping data values
		// Assume we have already scanned the entire table and populated our hash map
		if (_groupingAggregate._readAsInt)
		{
			ret = getNextGroup<int>(data, _intGrouping, _intGroupingIterator);
		}
		else
		{
			ret = getNextGroup<float>(data, _realGrouping, _realGroupingIterator);
		}
	}

	return ret;
}

void Aggregate::getAttributes(vector<Attribute>& attrs) const
{
	attrs.clear();
	
	// If we are grouping, we will output the grouping attributes first
	if (_hasGroup)
	{
		attrs.push_back(_groupAttribute);
	}

	// No matter what, we output the aggregation attribute
	Attribute attribute = _aggrigateAttribute;
	attribute.name = constructAggregateAttribute(_operation, _aggrigateAttribute.name);
	attrs.push_back(attribute);
}

std::string Aggregate::constructAggregateAttribute(AggregateOp op, const std::string& attributeName)
{
	std::stringstream s;
	s << getAggregateName(op);
	s << '(';
	s << attributeName;
	s << ')';

	return s.str();
}

std::string Aggregate::getAggregateName(AggregateOp op)
{
	switch (op)
	{
	case MIN:		return "MIN";
	case MAX:		return "MAX";
	case SUM:		return "SUM";
	case AVG:		return "AVG";
	case COUNT:		return "COUNT";
	default:		return "???";
	}
}
