#include "test_util.h"
#include "../util/returncodes.h"
#include "../rbf/rbfm.h"
#include <algorithm>

using namespace std;

void Tests_1();
void Tests_2();
void Tests_Custom();

struct RecData
{
    char* data;
    char* dataBuffer;
    RID rid;
    int size;
    int index;

    int uuid() const { return *(int*)(data); }
    char c() const { assert(  *(int*)(data+4) == 1 ); return *(char*)(data+8); }
    float f() const { return *(float*)(data+9); }
    int i() const { return *(int*)(data+13); }
    char* s() const { return (char*)(data+21); }
};

struct RecPlaceholder
{
    int uuid;
    char c;
    float f;
    int i;
    std::string s;
};

void generateSortableData(const vector<Attribute>& tupleDescriptor, std::vector<RecData>& recordData, const std::string& tableName, const int numRecords, const float forcedDupeChance)
{
    static int S_UUID = 0;
    RelationManager* rm = RelationManager::instance();

    std::cout << "Generating " << tableName << " data" << std::endl;

    // Clear out any old data (ignore errors because it may not exist)
    rm->deleteTable(tableName);

    // Create table
    RC ret = rm->createTable(tableName, tupleDescriptor);
    assert(ret == success);

    int index = 0;
    for (int i=0; i < numRecords; ++i)
    {
        // bad way of generating a random float, but it doesn't really matter
        const float r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
        int numRecordCopies = (r < forcedDupeChance) ? rand()%4 : 1;

        RecPlaceholder rec;
        rec.c = 'a' + (rand()%26);
        rec.uuid = S_UUID++;
        rec.i = rand();
        rec.f = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);

        int strlen = rand() % 3987;
        for (int s=0; s<strlen; ++s)
        {
            rec.s += 'A' + (rand()%26);
        }

        int recordLength = sizeof(float) + 4 * sizeof(int) + 1 + rec.s.length();
        for (int copy=0; copy<numRecordCopies; ++copy)
        {
            recordData.push_back(RecData());
            RecData& recData = recordData.back();
            recData.data = (char*)malloc(recordLength);
            recData.dataBuffer = (char*)malloc(recordLength);

            int dataOffset = 0;
            memcpy(recData.data + dataOffset, &rec.uuid, 4);
            dataOffset += 4;

            strlen = 1;
            memcpy(recData.data + dataOffset, &strlen, 4);
            dataOffset += 4;
            memcpy(recData.data + dataOffset, &rec.c, 1);
            dataOffset += 1;

            memcpy(recData.data + dataOffset, &rec.f, 4);
            dataOffset += 4;

            memcpy(recData.data + dataOffset, &rec.i, 4);
            dataOffset += 4;

            strlen = rec.s.length();
            memcpy(recData.data + dataOffset, &strlen, 4);
            dataOffset += 4;
            memcpy(recData.data + dataOffset, rec.s.c_str(), strlen);
            dataOffset += strlen;

            assert(dataOffset == recordLength);
            recData.index = index++;
            recData.size = recordLength;

            rm->insertTuple(tableName, recData.data, recData.rid);
        }
    }
}

void verifySortedDataExists(std::vector<RecData>& recordData, const std::string& tableName)
{
    std::cout << "Doing basic verification on " << tableName << " data" << std::endl;

    RelationManager* rm = RelationManager::instance();
    for(vector<RecData>::iterator it = recordData.begin(); it != recordData.end(); ++it)
    {
        RecData& rec = *it;
        RC ret = rm->readTuple(tableName, rec.rid, rec.dataBuffer);
        assert(ret == success);

        if (memcmp(rec.data, rec.dataBuffer, rec.size) != 0)
        {
            std::cout << "record mismatch: " << rec.index << std::endl;
            assert(false);
        }
    }
}

void verifyScan(vector<RecData>& records, RM_ScanIterator& scanner)
{
    static int debug = 0;
    ++debug;

    RID rid;
    char buffer[5000];

    vector<RecData>::iterator rec = records.end();
    vector<RecData>::iterator possible_rec = records.begin();

    int scanned = 0;
    while(scanner.getNextTuple(rid, buffer) == success)
    {
        if (possible_rec != records.end())
            ++possible_rec;

        ++scanned;

        // If it's just the next one, use it, otherwise we'll just do a linear search
        if (possible_rec != records.end() && possible_rec->rid.pageNum == rid.pageNum && possible_rec->rid.slotNum == rid.slotNum)
        {
            rec = possible_rec;
        }
        else
        {
            // Search for the record we just scanned in our list of verification records
            rec = records.end();
            for (vector<RecData>::iterator it = records.begin(); it != records.end(); ++it)
            {
                RecData& r = *it;
                if (r.rid.pageNum == rid.pageNum && r.rid.slotNum == rid.slotNum)
                {
                    rec = it;
                    break;
                }
            }
        }

        assert(rec != records.end());
        assert(memcmp(rec->data, buffer, rec->size) == 0);
    }

    assert(records.size() == scanned);
}

bool comp_lt_uuid(const RecData& rec, void* val) { return rec.uuid() <  *((int*)val); }
bool comp_gt_uuid(const RecData& rec, void* val) { return rec.uuid() >  *((int*)val); }
bool comp_le_uuid(const RecData& rec, void* val) { return rec.uuid() <= *((int*)val); }
bool comp_ge_uuid(const RecData& rec, void* val) { return rec.uuid() >= *((int*)val); }
bool comp_eq_uuid(const RecData& rec, void* val) { return rec.uuid() == *((int*)val); }
bool comp_ne_uuid(const RecData& rec, void* val) { return rec.uuid() != *((int*)val); }

bool comp_lt_i(const RecData& rec, void* val) { return rec.i() <  *((int*)val); }
bool comp_gt_i(const RecData& rec, void* val) { return rec.i() >  *((int*)val); }
bool comp_le_i(const RecData& rec, void* val) { return rec.i() <= *((int*)val); }
bool comp_ge_i(const RecData& rec, void* val) { return rec.i() >= *((int*)val); }
bool comp_eq_i(const RecData& rec, void* val) { return rec.i() == *((int*)val); }
bool comp_ne_i(const RecData& rec, void* val) { return rec.i() != *((int*)val); }

bool comp_lt_f(const RecData& rec, void* val) { return rec.f() <  *((float*)val); }
bool comp_gt_f(const RecData& rec, void* val) { return rec.f() >  *((float*)val); }
bool comp_le_f(const RecData& rec, void* val) { return rec.f() <= *((float*)val); }
bool comp_ge_f(const RecData& rec, void* val) { return rec.f() >= *((float*)val); }
bool comp_eq_f(const RecData& rec, void* val) { return rec.f() == *((float*)val); }
bool comp_ne_f(const RecData& rec, void* val) { return rec.f() != *((float*)val); }

bool comp_lt_c(const RecData& rec, void* val) { return rec.c() <  *((char*)val+4); }
bool comp_gt_c(const RecData& rec, void* val) { return rec.c() >  *((char*)val+4); }
bool comp_le_c(const RecData& rec, void* val) { return rec.c() <= *((char*)val+4); }
bool comp_ge_c(const RecData& rec, void* val) { return rec.c() >= *((char*)val+4); }
bool comp_eq_c(const RecData& rec, void* val) { return rec.c() == *((char*)val+4); }
bool comp_ne_c(const RecData& rec, void* val) { return rec.c() != *((char*)val+4); }

typedef bool (*comparefunc)(const RecData&,void*);

void verifySortOnValue(const vector<Attribute>& tupleDescriptor, std::vector<RecData>& recordData, const std::string& tableName, void* compValue, const std::string attrName, comparefunc f_lt, comparefunc f_gt, comparefunc f_le, comparefunc f_ge, comparefunc f_eq, comparefunc f_ne)
{
    RelationManager* rm = RelationManager::instance();
    vector<string> attributeNames;
    for (vector<Attribute>::const_iterator it = tupleDescriptor.begin(); it != tupleDescriptor.end(); ++it)
    {
        attributeNames.push_back(it->name);
    }

    vector<RecData> lt;
    vector<RecData> gt;
    vector<RecData> lte;
    vector<RecData> gte;
    vector<RecData> eq;
    vector<RecData> neq;

    for(std::vector<RecData>::iterator it = recordData.begin(); it != recordData.end(); ++it)
    {
        RecData& rec = *it;
        if (f_lt(rec, compValue))
            lt.push_back(rec);

        if (f_gt(rec, compValue))
            gt.push_back(rec);

        if (f_le(rec, compValue))
            lte.push_back(rec);

        if (f_ge(rec, compValue))
            gte.push_back(rec);

        if (f_eq(rec, compValue))
            eq.push_back(rec);

        if (f_ne(rec, compValue))
            neq.push_back(rec);
    }

    RM_ScanIterator scanLt;
    RM_ScanIterator scanGt;
    RM_ScanIterator scanLte;
    RM_ScanIterator scanGte;
    RM_ScanIterator scanEq;
    RM_ScanIterator scanNeq;

    RC ret;
    ret = rm->scan(tableName, attrName, LT_OP, compValue, attributeNames, scanLt);
    assert(ret == success);

    ret = rm->scan(tableName, attrName, GT_OP, compValue, attributeNames, scanGt);
    assert(ret == success);

    ret = rm->scan(tableName, attrName, LE_OP, compValue, attributeNames, scanLte);
    assert(ret == success);

    ret = rm->scan(tableName, attrName, GE_OP, compValue, attributeNames, scanGte);
    assert(ret == success);

    ret = rm->scan(tableName, attrName, EQ_OP, compValue, attributeNames, scanEq);
    assert(ret == success);

    ret = rm->scan(tableName, attrName, NE_OP, compValue, attributeNames, scanNeq);
    assert(ret == success);

    std::cout << " verify LT" << std::endl;
    verifyScan(lt,  scanLt);

    std::cout << " verify GT" << std::endl;
    verifyScan(gt,  scanGt);

    std::cout << " verify LTE" << std::endl;
    verifyScan(lte, scanLte);

    std::cout << " verify GTE" << std::endl;
    verifyScan(gte, scanGte);

    std::cout << " verify EQ" << std::endl;
    verifyScan(eq,  scanEq);

    std::cout << " verify NEQ" << std::endl;
    verifyScan(neq, scanNeq);
}

void verifySortOnUUID(const vector<Attribute>& tupleDescriptor, std::vector<RecData>& recordData, const std::string& tableName)
{
    int uuidCompare;

    std::cout << "Testing UUID scanning..." << std::endl;

    uuidCompare = 16;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &uuidCompare, "UUID",
                      comp_lt_uuid, comp_gt_uuid, comp_le_uuid, comp_ge_uuid, comp_eq_uuid, comp_ne_uuid);

    uuidCompare = 2;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &uuidCompare, "UUID",
                      comp_lt_uuid, comp_gt_uuid, comp_le_uuid, comp_ge_uuid, comp_eq_uuid, comp_ne_uuid);

    uuidCompare = 30;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &uuidCompare, "UUID",
                      comp_lt_uuid, comp_gt_uuid, comp_le_uuid, comp_ge_uuid, comp_eq_uuid, comp_ne_uuid);
}

void verifySortOnChar(const vector<Attribute>& tupleDescriptor, std::vector<RecData>& recordData, const std::string& tableName)
{
    char charComp[5];
    int* len = (int*)charComp;
    *len = 1;

    std::cout << "Testing char scanning..." << std::endl;

    charComp[4] = 'a';
    verifySortOnValue(tupleDescriptor, recordData, tableName, &charComp, "Char",
                      comp_lt_c, comp_gt_c, comp_le_c, comp_ge_c, comp_eq_c, comp_ne_c);

    charComp[4] = 'g';
    verifySortOnValue(tupleDescriptor, recordData, tableName, &charComp, "Char",
                      comp_lt_c, comp_gt_c, comp_le_c, comp_ge_c, comp_eq_c, comp_ne_c);

    charComp[4] = 'i';
    verifySortOnValue(tupleDescriptor, recordData, tableName, &charComp, "Char",
                      comp_lt_c, comp_gt_c, comp_le_c, comp_ge_c, comp_eq_c, comp_ne_c);
}

void verifySortOnInt(const vector<Attribute>& tupleDescriptor, std::vector<RecData>& recordData, const std::string& tableName)
{
    int intComp;

    std::cout << "Testing int scanning..." << std::endl;

    intComp = 9823754;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &intComp, "Int",
                      comp_lt_i, comp_gt_i, comp_le_i, comp_ge_i, comp_eq_i, comp_ne_i);

    intComp = 36292;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &intComp, "Int",
                      comp_lt_i, comp_gt_i, comp_le_i, comp_ge_i, comp_eq_i, comp_ne_i);

    intComp = 77;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &intComp, "Int",
                      comp_lt_i, comp_gt_i, comp_le_i, comp_ge_i, comp_eq_i, comp_ne_i);
}

void verifySortOnFloat(const vector<Attribute>& tupleDescriptor, std::vector<RecData>& recordData, const std::string& tableName)
{
    float floatComp;

    std::cout << "Testing float scanning..." << std::endl;

    floatComp = 0.0f;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &floatComp, "Float",
                      comp_lt_f, comp_gt_f, comp_le_f, comp_ge_f, comp_eq_f, comp_ne_f);

    floatComp = 0.5f;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &floatComp, "Float",
                      comp_lt_f, comp_gt_f, comp_le_f, comp_ge_f, comp_eq_f, comp_ne_f);

    floatComp = 0.1234f;
    verifySortOnValue(tupleDescriptor, recordData, tableName, &floatComp, "Float",
                      comp_lt_f, comp_gt_f, comp_le_f, comp_ge_f, comp_eq_f, comp_ne_f);
}

int main()
{
    // Basic Functions
    cout << endl << "Test Basic Functions..." << endl;

    Tests_Custom();

    // Create Table
    createTable("tbl_employee");

    Tests_1();
    Tests_2();

    return 0;
}

void doScanTest(std::vector<Attribute>& tupleDescriptor, const std::string tableName, int numRecords, float dupeChance)
{
    std::vector<RecData> r;
    generateSortableData(tupleDescriptor, r, tableName, numRecords, dupeChance);
    verifySortedDataExists(r, tableName);
    verifySortOnUUID(tupleDescriptor, r, tableName);
    verifySortOnChar(tupleDescriptor, r, tableName);
    verifySortOnInt(tupleDescriptor, r, tableName);
    verifySortOnFloat(tupleDescriptor, r, tableName);

    // Free up memory
    for(vector<RecData>::iterator it = r.begin(); it != r.end(); ++it)
    {
        free((*it).data);
        free((*it).dataBuffer);
    }
}

void Tests_Custom()
{
    // Set up the recordDescriptor
    std::vector<Attribute> tupleDescriptor;
    Attribute attr;

    attr.length = 4; attr.name = "UUID"; attr.type = TypeInt;  // This will be unique even across "dupe" records
    tupleDescriptor.push_back(attr);

    attr.length = 1; attr.name = "Char"; attr.type = TypeVarChar;
    tupleDescriptor.push_back(attr);

    attr.length = 4; attr.name = "Float"; attr.type = TypeReal;
    tupleDescriptor.push_back(attr);

    attr.length = 4; attr.name = "Int"; attr.type = TypeInt;
    tupleDescriptor.push_back(attr);

    attr.length = 3987; attr.name = "String"; attr.type = TypeVarChar;
    tupleDescriptor.push_back(attr);

    doScanTest(tupleDescriptor, "sortingTest1", 1024, 0.0f);
    doScanTest(tupleDescriptor, "sortingTest2", 55, 0.01f);
    doScanTest(tupleDescriptor, "sortingTest3", 3000, 0.02f);
    doScanTest(tupleDescriptor, "sortingTest4", 1000, 0.75f);
    doScanTest(tupleDescriptor, "sortingTest5", 250, 0.02f);
    doScanTest(tupleDescriptor, "sortingTest6", 123, 0.99f);
}

// tests from rmtest_1
void secA_0(const string &tableName)
{
    // Functions Tested
    // 1. Get Attributes
    cout << "****In Test Case 0****" << endl;

    // GetAttributes
    vector<Attribute> attrs;
    RC rc = rm->getAttributes(tableName, attrs);
    assert(rc == success);

    for(unsigned i = 0; i < attrs.size(); i++)
    {
        cout << "Attribute Name: " << attrs[i].name << endl;
        cout << "Attribute Type: " << (AttrType)attrs[i].type << endl;
        cout << "Attribute Length: " << attrs[i].length << endl << endl;
    }
    cout<<"** Test Case 0 passed"<<endl;
    return;
}


void secA_1(const string &tableName, const int nameLength, const string &name, const int age, const float height, const int salary)
{
    // Functions tested
    // 1. Create Table ** -- made separate now.
    // 2. Insert Tuple **
    // 3. Read Tuple **
    // NOTE: "**" signifies the new functions being tested in this test case. 
    cout << "****In Test Case 1****" << endl;
   
    RID rid; 
    int tupleSize = 0;
    void *tuple = malloc(100);
    void *returnedData = malloc(100);

    // Insert a tuple into a table
    prepareTuple(nameLength, name, age, height, salary, tuple, &tupleSize);
    cout << "Insert Data:" << endl;
    printTuple(tuple, tupleSize);
    RC rc = rm->insertTuple(tableName, tuple, rid);
    assert(rc == success);
    
    // Given the rid, read the tuple from table
    rc = rm->readTuple(tableName, rid, returnedData);
    assert(rc == success);

    cout << "Returned Data:" << endl;
    printTuple(returnedData, tupleSize);

    // Compare whether the two memory blocks are the same
    if(memcmp(tuple, returnedData, tupleSize) == 0)
    {
        cout << "****Test case 1 passed****" << endl << endl;
    }
    else
    {
        cout << "****Test case 1 failed****" << endl << endl;
    }

    free(tuple);
    free(returnedData);
    return;
}


void secA_2(const string &tableName, const int nameLength, const string &name, const int age, const float height, const int salary)
{
    // Functions Tested
    // 1. Insert tuple
    // 2. Delete Tuple **
    // 3. Read Tuple
    cout << "****In Test Case 2****" << endl;
   
    RID rid; 
    int tupleSize = 0;
    void *tuple = malloc(100);
    void *returnedData = malloc(100);

    // Test Insert the Tuple    
    prepareTuple(nameLength, name, age, height, salary, tuple, &tupleSize);
    cout << "Insert Data:" << endl;
    printTuple(tuple, tupleSize);
    RC rc = rm->insertTuple(tableName, tuple, rid);
    assert(rc == success);
    

    // Test Delete Tuple
    rc = rm->deleteTuple(tableName, rid);
    assert(rc == success);
    cout<< "delete data done"<<endl;
    
    // Test Read Tuple
    memset(returnedData, 0, 100);
    rc = rm->readTuple(tableName, rid, returnedData);
    assert(rc != success);

    cout << "After Deletion." << endl;
    
    // Compare the two memory blocks to see whether they are different
    if (memcmp(tuple, returnedData, tupleSize) != 0)
    {   
        cout << "****Test case 2 passed****" << endl << endl;
    }
    else
    {
        cout << "****Test case 2 failed****" << endl << endl;
    }
        
    free(tuple);
    free(returnedData);
    return;
}


void secA_3(const string &tableName, const int nameLength, const string &name, const int age, const float height, const int salary)
{
    // Functions Tested
    // 1. Insert Tuple    
    // 2. Update Tuple **
    // 3. Read Tuple
    cout << "****In Test Case 3****" << endl;
   
    RID rid; 
    int tupleSize = 0;
    int updatedTupleSize = 0;
    void *tuple = malloc(100);
    void *updatedTuple = malloc(100);
    void *returnedData = malloc(100);
   
    // Test Insert Tuple 
    prepareTuple(nameLength, name, age, height, salary, tuple, &tupleSize);
    RC rc = rm->insertTuple(tableName, tuple, rid);
    assert(rc == success);
    cout << "Original RID slot = " << rid.slotNum << endl;

    // Test Update Tuple
    prepareTuple(6, "Newman", age, height, 100, updatedTuple, &updatedTupleSize);
    rc = rm->updateTuple(tableName, updatedTuple, rid);
    assert(rc == success);
    cout << "Updated RID slot = " << rid.slotNum << endl;

    // Test Read Tuple 
    rc = rm->readTuple(tableName, rid, returnedData);
    assert(rc == success);
    cout << "Read RID slot = " << rid.slotNum << endl;
   
    // Print the tuples 
    cout << "Insert Data:" << endl; 
    printTuple(tuple, tupleSize);

    cout << "Updated data:" << endl;
    printTuple(updatedTuple, updatedTupleSize);

    cout << "Returned Data:" << endl;
    printTuple(returnedData, updatedTupleSize);
    
    if (memcmp(updatedTuple, returnedData, updatedTupleSize) == 0)
    {
        cout << "****Test case 3 passed****" << endl << endl;
    }
    else
    {
        cout << "****Test case 3 failed****" << endl << endl;
    }

    free(tuple);
    free(updatedTuple);
    free(returnedData);
    return;
}


void secA_4(const string &tableName, const int nameLength, const string &name, const int age, const float height, const int salary)
{
    // Functions Tested
    // 1. Insert tuple
    // 2. Read Attributes **
    cout << "****In Test Case 4****" << endl;
    
    RID rid;    
    int tupleSize = 0;
    void *tuple = malloc(100);
    void *returnedData = malloc(100);
    
    // Test Insert Tuple 
    prepareTuple(nameLength, name, age, height, salary, tuple, &tupleSize);
    RC rc = rm->insertTuple(tableName, tuple, rid);
    assert(rc == success);

    // Test Read Attribute
    rc = rm->readAttribute(tableName, rid, "Salary", returnedData);
    assert(rc == success);
 
    cout << "Salary: " << *(int *)returnedData << endl;
    if (memcmp((char *)returnedData, (char *)tuple+18, 4) != 0)
    {
        cout << "****Test case 4 failed" << endl << endl;
    }
    else
    {
        cout << "****Test case 4 passed" << endl << endl;
    }
    
    free(tuple);
    free(returnedData);
    return;
}


void secA_5(const string &tableName, const int nameLength, const string &name, const int age, const float height, const int salary)
{
    // Functions Tested
    // 0. Insert tuple;
    // 1. Read Tuple
    // 2. Delete Tuples **
    // 3. Read Tuple
    cout << "****In Test Case 5****" << endl;
    
    RID rid;
    int tupleSize = 0;
    void *tuple = malloc(100);
    void *returnedData = malloc(100);
    void *returnedData1 = malloc(100);
   
    // Test Insert Tuple 
    prepareTuple(nameLength, name, age, height, salary, tuple, &tupleSize);
    RC rc = rm->insertTuple(tableName, tuple, rid);
    assert(rc == success);

    // Test Read Tuple
    rc = rm->readTuple(tableName, rid, returnedData);
    assert(rc == success);
    printTuple(returnedData, tupleSize);

    cout << "Now Deleting..." << endl;

    // Test Delete Tuples
    rc = rm->deleteTuples(tableName);
    assert(rc == success);
    
    // Test Read Tuple
    memset((char*)returnedData1, 0, 100);
    rc = rm->readTuple(tableName, rid, returnedData1);
    assert(rc != success);
    printTuple(returnedData1, tupleSize);
    
    if(memcmp(tuple, returnedData1, tupleSize) != 0)
    {
        cout << "****Test case 5 passed****" << endl << endl;
    }
    else
    {
        cout << "****Test case 5 failed****" << endl << endl;
    }
       
    free(tuple);
    free(returnedData);
    free(returnedData1);
    return;
}


void secA_6(const string &tableName, const int nameLength, const string &name, const int age, const float height, const int salary)
{
    // Functions Tested
    // 0. Insert tuple;
    // 1. Read Tuple
    // 2. Delete Table **
    // 3. Read Tuple
    cout << "****In Test Case 6****" << endl;
   
    RID rid; 
    int tupleSize = 0;
    void *tuple = malloc(100);
    void *returnedData = malloc(100);
    void *returnedData1 = malloc(100);
   
    // Test Insert Tuple
    prepareTuple(nameLength, name, age, height, salary, tuple, &tupleSize);
    RC rc = rm->insertTuple(tableName, tuple, rid);
    assert(rc == success);

    // Test Read Tuple 
    rc = rm->readTuple(tableName, rid, returnedData);
    assert(rc == success);

    // Test Delete Table
    rc = rm->deleteTable(tableName);
    cout << rc::rcToString(rc) << endl;
    assert(rc == success);
    cout << "After deletion!" << endl;
    
    // Test Read Tuple 
    memset((char*)returnedData1, 0, 100);
    rc = rm->readTuple(tableName, rid, returnedData1);
    assert(rc != success);
    
    if(memcmp(returnedData, returnedData1, tupleSize) != 0)
    {
        cout << "****Test case 6 passed****" << endl << endl;
    }
    else
    {
        cout << "****Test case 6 failed****" << endl << endl;
    }
        
    free(tuple);
    free(returnedData);    
    free(returnedData1);
    return;
}


void secA_7(const string &tableName)
{
    // Functions Tested
    // 1. Reorganize Page **
    // Insert tuples into one page, reorganize that page, 
    // and use the same tids to read data. The results should 
    // be the same as before. Will check code as well.
    cout << "****In Test Case 7****" << endl;
   
    RID rid; 
    int tupleSize = 0;
    const int numTuples = 5;
    void *tuple;
    void *returnedData = malloc(100);

    int sizes[numTuples];
    RID rids[numTuples];
    vector<char *> tuples;

    RC rc = 0;
    for(int i = 0; i < numTuples; i++)
    {
        tuple = malloc(100);

        // Test Insert Tuple
        float height = (float)i;
        prepareTuple(6, "Tester", 20+i, height, 123, tuple, &tupleSize);
        rc = rm->insertTuple(tableName, tuple, rid);
        assert(rc == success);

        tuples.push_back((char *)tuple);
        sizes[i] = tupleSize;
        rids[i] = rid;
        if (i > 0) {
            // Since we are inserting 5 tiny tuples into an empty table where the page size is 4kb, all the 5 tuples should be on the first page. 
            assert(rids[i - 1].pageNum == rids[i].pageNum);
        }
        cout << rid.pageNum << endl;
    }
    cout << "After Insertion!" << endl;
    
    int pageid = rid.pageNum;
    rc = rm->reorganizePage(tableName, pageid);
    assert(rc == success);

    // Print out the tuples one by one
    int i = 0;
    for (i = 0; i < numTuples; i++)
    {
        rc = rm->readTuple(tableName, rids[i], returnedData);
        assert(rc == success);
        printTuple(returnedData, tupleSize);

        //if any of the tuples are not the same as what we entered them to be ... there is a problem with the reorganization.
        if (memcmp(tuples[i], returnedData, sizes[i]) != 0)
        {      
            cout << "****Test case 7 failed****" << endl << endl;
            break;
        }
    }
    if(i == numTuples)
    {
        cout << "****Test case 7 passed****" << endl << endl;
    }
    
    // Delete Table    
    rc = rm->deleteTable(tableName);
    assert(rc == success);

    free(returnedData);
    for(i = 0; i < numTuples; i++)
    {
        free(tuples[i]);
    }
    return;
}


void secA_8_A(const string &tableName)
{
    // Functions Tested
    // 1. Simple scan **
    cout << "****In Test Case 8_A****" << endl;

    RID rid;    
    int tupleSize = 0;
    const int numTuples = 100;
    void *tuple;
    void *returnedData = malloc(100);

    RID rids[numTuples];
    vector<char *> tuples;
    set<int> ages; 
    RC rc = 0;
    for(int i = 0; i < numTuples; i++)
    {
        tuple = malloc(100);

        // Insert Tuple
        float height = (float)i;
        int age = 20+i;
        prepareTuple(6, "Tester", age, height, 123, tuple, &tupleSize);
        ages.insert(age);
        rc = rm->insertTuple(tableName, tuple, rid);
        assert(rc == success);

        tuples.push_back((char *)tuple);
        rids[i] = rid;
    }
    cout << "After Insertion!" << endl;

    // Set up the iterator
    RM_ScanIterator rmsi;
    string attr = "Age";
    vector<string> attributes;
    attributes.push_back(attr);
    rc = rm->scan(tableName, "", NO_OP, NULL, attributes, rmsi);
    assert(rc == success);

    cout << "Scanned Data:" << endl;
    
    while(rmsi.getNextTuple(rid, returnedData) != RM_EOF)
    {
        cout << "Age: " << *(int *)returnedData << endl;
        if (ages.find(*(int *)returnedData) == ages.end())
        {
            cout << "****Test case 8_A failed****" << endl << endl;
            rmsi.close();
            free(returnedData);
            for(int i = 0; i < numTuples; i++)
            {
                free(tuples[i]);
            }
            return;
        }
    }
    rmsi.close();

    free(returnedData);
    for(int i = 0; i < numTuples; i++)
    {
        free(tuples[i]);
    }
    cout << "****Test case 8_A passed****" << endl << endl; 
    return;
}

void Tests_1()
{
    // GetAttributes
    secA_0("tbl_employee");

    // Insert/Read Tuple
    secA_1("tbl_employee", 6, "Peters", 24, 170.1, 5000);

    // Delete Tuple
    secA_2("tbl_employee", 6, "Victor", 22, 180.2, 6000);

    // Update Tuple
    secA_3("tbl_employee", 6, "Thomas", 28, 187.3, 4000);

    // Read Attributes
    secA_4("tbl_employee", 6, "Veekay", 27, 171.4, 9000);

    // Delete Tuples
    secA_5("tbl_employee", 6, "Dillon", 29, 172.5, 7000);

    // Delete Table
    secA_6("tbl_employee", 6, "Martin", 26, 173.6, 8000);
   
    memProfile();
 
    // Reorganize Page
    createTable("tbl_employee2");
    secA_7("tbl_employee2");

    // Simple Scan
    createTable("tbl_employee3");
    secA_8_A("tbl_employee3");

    memProfile();
    return;
}


// tests from rmtest_2
void secA_8_B(const string &tableName)
{
    // Functions Tested
    // 1. Simple scan **
    cout << "****In Test Case 8_B****" << endl;

    RID rid;    
    const int numTuples = 100;
    void *returnedData = malloc(100);

    set<int> ages; 
    RC rc = 0;
    for(int i = 0; i < numTuples; i++)
    {
        int age = 20+i;
        ages.insert(age);
    }

    // Set up the iterator
    RM_ScanIterator rmsi;
    string attr = "Age";
    vector<string> attributes;
    attributes.push_back(attr);
    rc = rm->scan(tableName, "", NO_OP, NULL, attributes, rmsi);
    assert(rc == success);

    cout << "Scanned Data:" << endl;
    
    while(rmsi.getNextTuple(rid, returnedData) != RM_EOF)
    {
        cout << "Age: " << *(int *)returnedData << endl;
        if (ages.find(*(int *)returnedData) == ages.end())
        {
            cout << "****Test case 8_B failed****" << endl << endl;
            rmsi.close();
            free(returnedData);
            return;
        }
    }
    rmsi.close();
    
    // Delete a Table
    rc = rm->deleteTable(tableName);
    assert(rc == success);

    free(returnedData);
    cout << "****Test case 8_B passed****" << endl << endl; 
    return;
}


void secA_9(const string &tableName, vector<RID> &rids, vector<int> &sizes)
{
    // Functions Tested:
    // 1. create table
    // 2. getAttributes
    // 3. insert tuple
    cout << "****In Test case 9****" << endl;

    RID rid; 
    void *tuple = malloc(1000);
    const int numTuples = 2000;

    // GetAttributes
    vector<Attribute> attrs;
    RC rc = rm->getAttributes(tableName, attrs);
    assert(rc == success);

    for(unsigned i = 0; i < attrs.size(); i++)
    {
        cout << "Attribute Name: " << attrs[i].name << endl;
        cout << "Attribute Type: " << (AttrType)attrs[i].type << endl;
        cout << "Attribute Length: " << attrs[i].length << endl << endl;
    }

    // Insert 2000 tuples into table
    for(int i = 0; i < numTuples; i++)
    {
        // Test insert Tuple
        int size = 0;
        memset(tuple, 0, 1000);
        prepareLargeTuple(i, tuple, &size);

        rc = rm->insertTuple(tableName, tuple, rid);
        assert(rc == success);

        rids.push_back(rid);
        sizes.push_back(size);        
    }
    cout << "****Test case 9 passed****" << endl << endl;

    free(tuple);
}


void secA_10(const string &tableName, const vector<RID> &rids, const vector<int> &sizes)
{
    // Functions Tested:
    // 1. read tuple
    cout << "****In Test case 10****" << endl;

    const int numTuples = 2000;
    void *tuple = malloc(1000);
    void *returnedData = malloc(1000);

    RC rc = 0;
    for(int i = 0; i < numTuples; i++)
    {
        memset(tuple, 0, 1000);
        memset(returnedData, 0, 1000);
        rc = rm->readTuple(tableName, rids[i], returnedData);
        assert(rc == success);

        int size = 0;
        prepareLargeTuple(i, tuple, &size);
        if(memcmp(returnedData, tuple, sizes[i]) != 0)
        {
            cout << "****Test case 10 failed****" << endl << endl;
            return;
        }
    }
    cout << "****Test case 10 passed****" << endl << endl;

    free(tuple);
    free(returnedData);
}


void secA_11(const string &tableName, vector<RID> &rids, vector<int> &sizes)
{
    // Functions Tested:
    // 1. update tuple
    // 2. read tuple
    cout << "****In Test case 11****" << endl;

    RC rc = 0;
    void *tuple = malloc(1000);
    void *returnedData = malloc(1000);

    // Update the first 1000 tuples
    int size = 0;
    for(int i = 0; i < 1000; i++)
    {
        memset(tuple, 0, 1000);
        RID rid = rids[i];

        prepareLargeTuple(i+10, tuple, &size);
        rc = rm->updateTuple(tableName, tuple, rid);
        assert(rc == success);

        sizes[i] = size;
        rids[i] = rid;
    }
    cout << "Updated!" << endl;

    // Read the recrods out and check integrity
    for(int i = 0; i < 1000; i++)
    {
        memset(tuple, 0, 1000);
        memset(returnedData, 0, 1000);
        prepareLargeTuple(i+10, tuple, &size);
        rc = rm->readTuple(tableName, rids[i], returnedData);
        assert(rc == success);

        if(memcmp(returnedData, tuple, sizes[i]) != 0)
        {
            cout << "****Test case 11 failed****" << endl << endl;
            return;
        }
    }
    cout << "****Test case 11 passed****" << endl << endl;

    free(tuple);
    free(returnedData);
}


void secA_12(const string &tableName, const vector<RID> &rids)
{
    // Functions Tested
    // 1. delete tuple
    // 2. read tuple
    cout << "****In Test case 12****" << endl;

    RC rc = 0;
    void * returnedData = malloc(1000);

    // Delete the first 1000 tuples
    for(int i = 0; i < 1000; i++)
    {
        // cout << "deleting rid: " << rids[i].pageNum << "," << rids[i].slotNum << endl;
        rc = rm->deleteTuple(tableName, rids[i]);
        // cout << "new rids: " << rids[i].pageNum << "," << rids[i].slotNum << endl;
        assert(rc == success);

        rc = rm->readTuple(tableName, rids[i], returnedData);
        assert(rc != success);
    }
    cout << "After deletion!" << endl;

    for(int i = 1000; i < 2000; i++)
    {
        rc = rm->readTuple(tableName, rids[i], returnedData);
        assert(rc == success);
    }
    cout << "****Test case 12 passed****" << endl << endl;

    free(returnedData);
}


void secA_13(const string &tableName)
{
    // Functions Tested
    // 1. scan
    cout << "****In Test case 13****" << endl;

    RM_ScanIterator rmsi;
    vector<string> attrs;
    attrs.push_back("attr5");
    attrs.push_back("attr12");
    attrs.push_back("attr28");
   
    RC rc = rm->scan(tableName, "", NO_OP, NULL, attrs, rmsi); 
    assert(rc == success);

    RID rid;
    int j = 0;
    void *returnedData = malloc(1000);

    rc = rmsi.getNextTuple(rid, returnedData);
    while(rmsi.getNextTuple(rid, returnedData) != RM_EOF)
    {
        if(j % 200 == 0)
        {
            int offset = 0;

            cout << "Real Value: " << *(float *)(returnedData) << endl;
            offset += 4;
        
            int size = *(int *)((char *)returnedData + offset);
            cout << "String size: " << size << endl;
            offset += 4;

            char *buffer = (char *)malloc(size + 1);
            memcpy(buffer, (char *)returnedData + offset, size);
            buffer[size] = 0;
            offset += size;
    
            cout << "Char Value: " << buffer << endl;

            cout << "Integer Value: " << *(int *)((char *)returnedData + offset ) << endl << endl;
            offset += 4;

            free(buffer);
        }
        j++;
        memset(returnedData, 0, 1000);
        rc = rmsi.getNextTuple(rid, returnedData);
    }
    rmsi.close();
    cout << "Total number of tuples: " << j << endl << endl;

    cout << "****Test case 13 passed****" << endl << endl;
    free(returnedData);
}


void secA_14(const string &tableName, const vector<RID> &rids)
{
    // Functions Tested
    // 1. reorganize page
    // 2. delete tuples
    // 3. delete table
    cout << "****In Test case 14****" << endl;

    RC rc;
    rc = rm->reorganizePage(tableName, rids[1000].pageNum);
    assert(rc == success);

    rc = rm->deleteTuples(tableName);
    assert(rc == success);

    rc = rm->deleteTable(tableName);
    assert(rc == success);

    cout << "****Test case 14 passed****" << endl << endl;
}


void secA_15(const string &tableName) {

    cout << "****In Test case 15****" << endl;
    
    RID rid;    
    int tupleSize = 0;
    const int numTuples = 500;
    void *tuple;
    void *returnedData = malloc(100);
    int ageVal = 25;

    RID rids[numTuples];
    vector<char *> tuples;

    RC rc = 0;
    int age;
    for(int i = 0; i < numTuples; i++)
    {
        tuple = malloc(100);

        // Insert Tuple
        float height = (float)i;
        
        age = (rand()%20) + 15;
        
        prepareTuple(6, "Tester", age, height, 123, tuple, &tupleSize);
        rc = rm->insertTuple(tableName, tuple, rid);
        assert(rc == success);

        tuples.push_back((char *)tuple);
        rids[i] = rid;
    }
    cout << "After Insertion!" << endl;

    // Set up the iterator
    RM_ScanIterator rmsi;
    string attr = "Age";
    vector<string> attributes;
    attributes.push_back(attr); 
    rc = rm->scan(tableName, attr, GT_OP, &ageVal, attributes, rmsi);
    assert(rc == success); 

    cout << "Scanned Data:" << endl;
    
    while(rmsi.getNextTuple(rid, returnedData) != RM_EOF)
    {
        cout << "Age: " << *(int *)returnedData << endl;
        assert ( (*(int *) returnedData) > ageVal );
    }
    rmsi.close();
    
    // Deleta Table
    rc = rm->deleteTable(tableName);
    assert(rc == success);

    free(returnedData);
    for(int i = 0; i < numTuples; i++)
    {
        free(tuples[i]);
    }
    
    cout << "****Test case 15 passed****" << endl << endl;
}


void Tests_2()
{
    // Simple Scan
    secA_8_B("tbl_employee3");

    memProfile();
    
    // Pressure Test
    createLargeTable("tbl_employee4");

    vector<RID> rids;
    vector<int> sizes;

    // Insert Tuple
    secA_9("tbl_employee4", rids, sizes);
    // Read Tuple
    secA_10("tbl_employee4", rids, sizes);

    // Update Tuple
    secA_11("tbl_employee4", rids, sizes);

    // Delete Tuple
    secA_12("tbl_employee4", rids);

    memProfile();

    // Scan
    secA_13("tbl_employee4");

    // DeleteTuples/Table
    secA_14("tbl_employee4", rids);
    
    // Scan with conditions
    createTable("tbl_b_employee4");  
    secA_15("tbl_b_employee4");
    
    memProfile();
    return;
}
