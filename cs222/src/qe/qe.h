#ifndef _qe_h_
#define _qe_h_

#include <vector>

#include "../rbf/rbfm.h"
#include "../rm/rm.h"
#include "../ix/ix.h"

# define QE_EOF (-1)  // end of the index scan

using namespace std;

typedef enum{ MIN = 0, MAX, SUM, AVG, COUNT } AggregateOp;


// The following functions use  the following
// format for the passed data.
//    For int and real: use 4 bytes
//    For varchar: use 4 bytes for the length followed by
//                          the characters

struct Value {
    AttrType type;          // type of value
    void     *data;         // value
};


struct Condition {
    string lhsAttr;         // left-hand side attribute
    CompOp  op;             // comparison operator
    bool    bRhsIsAttr;     // TRUE if right-hand side is an attribute and not a value; FALSE, otherwise.
    string rhsAttr;         // right-hand side attribute if bRhsIsAttr = TRUE
    Value   rhsValue;       // right-hand side value if bRhsIsAttr = FALSE

	bool compare(const void* thatData) const;
	bool compare(AttrType type, const void* left, const void* right) const;

	static RC splitAttr(const string& input, string& rel, string& attr);
};


class Iterator {
    // All the relational operators and access methods are iterators.
    public:
        virtual RC getNextTuple(void *data) = 0;
        virtual void getAttributes(vector<Attribute> &attrs) const = 0;
        virtual ~Iterator() {};
};


class TableScan : public Iterator
{
    // A wrapper inheriting Iterator over RM_ScanIterator
    public:
        RelationManager &rm;
        RM_ScanIterator *iter;
        string tableName;
        vector<Attribute> attrs;
        vector<string> attrNames;
        RID rid;

        TableScan(RelationManager &rm, const string &tableName, const char *alias = NULL):rm(rm)
        {
        	//Set members
        	this->tableName = tableName;

            // Get Attributes from RM
            rm.getAttributes(tableName, attrs);

            // Get Attribute Names from RM
            unsigned i;
            for(i = 0; i < attrs.size(); ++i)
            {
                // convert to char *
                attrNames.push_back(attrs[i].name);
            }

            // Call rm scan to get iterator
            iter = new RM_ScanIterator();
            rm.scan(tableName, "", NO_OP, NULL, attrNames, *iter);

            // Set alias
            if(alias) this->tableName = alias;
        };

        // Start a new iterator given the new compOp and value
        void setIterator()
        {
            iter->close();
            delete iter;
            iter = new RM_ScanIterator();
            rm.scan(tableName, "", NO_OP, NULL, attrNames, *iter);
        };

        RC getNextTuple(void *data)
        {
            return iter->getNextTuple(rid, data);
        };

        void getAttributes(vector<Attribute> &attrs) const
        {
            attrs.clear();
            attrs = this->attrs;
            unsigned i;

            // For attribute in vector<Attribute>, name it as rel.attr
            for(i = 0; i < attrs.size(); ++i)
            {
                string tmp = tableName;
                tmp += ".";
                tmp += attrs[i].name;
                attrs[i].name = tmp;
            }
        };

        ~TableScan()
        {
        	iter->close();
        };
};


class IndexScan : public Iterator
{
    // A wrapper inheriting Iterator over IX_IndexScan
    public:
        RelationManager &rm;
        RM_IndexScanIterator *iter;
        string tableName;
        string attrName;
        vector<Attribute> attrs;
        char key[PAGE_SIZE];
        RID rid;

        IndexScan(RelationManager &rm, const string &tableName, const string &attrName, const char *alias = NULL):rm(rm)
        {
        	// Set members
        	this->tableName = tableName;
        	this->attrName = attrName;


            // Get Attributes from RM
            rm.getAttributes(tableName, attrs);

            // Call rm indexScan to get iterator
            iter = new RM_IndexScanIterator();
            rm.indexScan(tableName, attrName, NULL, NULL, true, true, *iter);

            // Set alias
            if(alias) this->tableName = alias;
        };

        // Start a new iterator given the new key range
        void setIterator(void* lowKey,
                         void* highKey,
                         bool lowKeyInclusive,
                         bool highKeyInclusive)
        {
            iter->close();
            delete iter;
            iter = new RM_IndexScanIterator();
            rm.indexScan(tableName, attrName, lowKey, highKey, lowKeyInclusive,
                           highKeyInclusive, *iter);
        };

        RC getNextTuple(void *data)
        {
            int rc = iter->getNextEntry(rid, key);
            if(rc == 0)
            {
                rc = rm.readTuple(tableName.c_str(), rid, data);
            }
            return rc;
        };

        void getAttributes(vector<Attribute> &attrs) const
        {
            attrs.clear();
            attrs = this->attrs;
            unsigned i;

            // For attribute in vector<Attribute>, name it as rel.attr
            for(i = 0; i < attrs.size(); ++i)
            {
                string tmp = tableName;
                tmp += ".";
                tmp += attrs[i].name;
                attrs[i].name = tmp;
            }
        };

        ~IndexScan()
        {
            iter->close();
        };
};


class Filter : public Iterator {
    // Filter operator
    public:
        Filter(Iterator *input,                         // Iterator of input R
               const Condition &condition               // Selection condition
        );
        ~Filter(){};

		RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
		void getAttributes(vector<Attribute> &attrs) const;

		RC getDataOffset(const vector<Attribute>& attrs, unsigned attrIndex, const void* data, unsigned& dataOffset);

private:
	Iterator* _input;
	Condition _condition;
	unsigned _lhsAttrIndex;
	unsigned _rhsAttrIndex;
	std::vector<Attribute> _lhsAttributes;
};


class Project : public Iterator {
    // Projection operator
    public:
        Project(Iterator *input,                            // Iterator of input R
                const vector<string> &attrNames);           // vector containing attribute names
        ~Project();

        RC getNextTuple(void *data);
        // For attribute in vector<Attribute>, name it as rel.attr
        void getAttributes(vector<Attribute> &attrs) const;

    protected:
        unsigned _entrySize;
        void* _entryBuffer;
        vector<string> _projectAttributeNames;
        vector<Attribute> _projectAttributes;
        vector<Attribute> _itrAttributes;
        Iterator* _itr;
};

struct JoinerData
{
	Iterator* iter;
	vector<Attribute> attributes;
	unsigned attributeIndex;
	unsigned attributeOffset;
	RC status;

	JoinerData(Iterator* in, const string& attributeName);
	RC advance(void* data);
};

class Joiner : public Iterator {
public:
	Joiner(
		Iterator *leftIn,                             // Iterator of input R
		Iterator *rightIn,                            // Iterator of input S
		const Condition &condition,                   // Join condition
		const unsigned numPages                       // Number of pages can be used to do join (decided by the optimizer)
		);

	virtual ~Joiner();

	virtual RC resetInner() = 0;

	RC getNextTuple(void *data);

	// For attribute in vector<Attribute>, name it as rel.attr
	void getAttributes(vector<Attribute> &attrs) const;

	RC copyoutData(void* dataOut, const void* dataOuter, const void* dataInner);
	static RC compareData(const Condition& condition, AttrType attrType, const void* dataLeft, const void* dataRight);

protected:
	inline char* getPage(unsigned pageNum) { return _pageBuffer + (pageNum * PAGE_SIZE);  }
	inline void clearPage(unsigned pageNum) { memset(getPage(pageNum), 0, PAGE_SIZE); }
	inline void setPage(unsigned pageNum, void* data) { memcpy(getPage(pageNum), data, PAGE_SIZE); }

	JoinerData _outer;
	JoinerData _inner;
	
	AttrType _attrType;
	const Condition _condition;
	const unsigned _numPages;
	char* _pageBuffer;
};

class NLJoin : public Joiner {
    // Nested-Loop join operator
    public:
		NLJoin(Iterator* leftIn, TableScan* rightIn, const Condition& condition, const unsigned numPages)
			: Joiner(leftIn, rightIn, condition, numPages) { }

		virtual ~NLJoin() { }

		virtual RC resetInner();
};


class INLJoin : public Joiner {
    // Index Nested-Loop join operator
    public:
		INLJoin(Iterator* leftIn, IndexScan* rightIn, const Condition& condition, const unsigned numPages)
			: Joiner(leftIn, rightIn, condition, numPages) { }

		virtual ~INLJoin() { }

		virtual RC resetInner();
};

struct AggregateData
{
	void init(AttrType type, AggregateOp op);
	void write(void* dest) const;
	void read(const void* data, unsigned attributeOffset, float& f, int& i);
	void append(AggregateOp op, float realData, int intData);

	float _realValue;
	int _intValue;
	unsigned _count;
	
	char _groupingData[4];
	bool _readAsInt;
	bool _writeAsInt;
};

class Aggregate : public Iterator {
    // Aggregation operator
    public:
        Aggregate(Iterator* input,                              // Iterator of input R
                  Attribute aggAttr,                            // The attribute over which we are computing an aggregate
                  AggregateOp op                                // Aggregate operation
        );

        // Extra Credit
        Aggregate(Iterator* input,                              // Iterator of input R
                  Attribute aggAttr,                            // The attribute over which we are computing an aggregate
                  Attribute gAttr,                              // The attribute over which we are grouping the tuples
                  AggregateOp op                                // Aggregate operation
        );

        ~Aggregate(){};

		RC getNextTuple(void* data);

        // Please name the output attribute as aggregateOp(aggAttr)
        // E.g. Relation=rel, attribute=attr, aggregateOp=MAX
        // output attrname = "MAX(rel.attr)"
        void getAttributes(vector<Attribute>& attrs) const;

private:
	static std::string constructAggregateAttribute(AggregateOp op, const std::string& attributeName);
	static std::string getAggregateName(AggregateOp op);

	template <typename T>
	AggregateData* getGroupedAggregate(T value, std::map<T, AggregateData>& groupMap);

	template <typename T>
	RC getNextGroup(void* data, std::map<T, AggregateData>& groupMap, typename std::map<T, AggregateData>::const_iterator& groupIter);

	RC getNextSingleTuple();

	Iterator* _input;
	Attribute _aggrigateAttribute;
	Attribute _groupAttribute;
	AggregateOp _operation;

	vector<Attribute> _attributes;
	unsigned _aggrigateAttributeIndex;
	unsigned _groupAttributeIndex;

	AggregateData _totalAggregate;
	AggregateData _groupingAggregate; // used only for metadata

	std::map<float, AggregateData> _realGrouping;
	std::map<int, AggregateData> _intGrouping;

	std::map<float, AggregateData>::const_iterator _realGroupingIterator;
	std::map<int, AggregateData>::const_iterator _intGroupingIterator;

	bool _hasGroup;
	char _buffer[PAGE_SIZE];
};

#endif
