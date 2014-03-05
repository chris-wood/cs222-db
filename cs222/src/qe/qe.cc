#include "qe.h"
#include <assert.h>

Filter::Filter(Iterator* input, const Condition &condition)
: _input(input),
_condition(condition)
{
	// TODO: What does this mean for a Filter if this is true?
	assert(_condition.bRhsIsAttr == false);

	RC ret = Condition::splitAttr(_condition.lhsAttr, _lhsRel, _lhsAttr);
	assert(ret == rc::OK);

	ret = Condition::splitAttr(_condition.rhsAttr, _rhsRel, _rhsAttr);
	assert(ret == rc::OK);
}

bool Condition::compare(void* thatData) const
{
	return RBFM_ScanIterator::compareData(rhsValue.type, op, thatData, rhsValue.data);
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

RC Filter::getNextTuple(void *data)
{
	RC ret = rc::UNKNOWN_FAILURE;
	
	// Iterate until we find a valid tuple, or we have no more items to iterate through
	while (ret != rc::OK && ret != QE_EOF)
	{
		ret = _input->getNextTuple(data);
		if (_condition.compare(data))
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
