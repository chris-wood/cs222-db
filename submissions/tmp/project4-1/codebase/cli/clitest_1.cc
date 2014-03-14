#include <cassert>

#include "cli.h"

using namespace std;

#define SUCCESS 0

typedef void(*fn_test)(bool);

CLI *cli;

void exec(bool print, string command, bool equal = true)
{
	if (print)
		cout << ">>> " << command << endl;

	if( equal )
		assert (cli->process(command) == SUCCESS);
	else
		assert (cli->process(command) != SUCCESS);
}

void RunTest(const std::string& testName, fn_test test, int numRetries)
{
	cout << "******************** CLI " << testName << "********************\n\n";

	if (numRetries < 1)
	{
		cout << "Not running test, numRetries=" << numRetries << endl;
	}
	else
	{
		test(true);
	}
	
	cout << "\n=========================================" << endl;
	cout << "Repeating test " << (numRetries-1) << " times...\n";
	for (int i=1; i<numRetries; ++i)
	{
		test(false);
	}

	cout << endl;
}

void CreateDeleteTest(bool print)
{
	if (print)
	{
		cout << "* We will create a bunch of empty tables/indexes and delete them" << endl;
		cout << "* Creating initial tables..." << endl;
	}

	// Create various tables
	exec(print, "create table bunch_of_nums n1 = int, n2 = int, n3 = int, n4 = int, n5 = int, n6 = real");
	exec(print, "create table bunch_of_strings s1 = varchar(1), s2 = varchar(2), s3 = varchar(50), s4 = varchar(500)");
	exec(print, "create table bunch_of_stuff foo = int, bar = varchar(50), biz = real, baz = real, qux = varchar(10), bad = int, good = real");

	// Delete all of the tables
	if (print)
		cout << "* Deleting initial tables..." << endl;
	exec(print, "drop table bunch_of_nums");
	exec(print, "drop table bunch_of_strings");
	exec(print, "drop table bunch_of_stuff");

	// Recreate the tables we had before, but with different columns
	if (print)
		cout << "* Recreating tables with different columns..." << endl;
	exec(print, "create table bunch_of_nums n1 = real, n2 = real, n3 = int, n4 = int, n5 = int, n6 = int");
	exec(print, "create table bunch_of_strings s1 = varchar(11), s2 = varchar(100), s3 = varchar(4), s4 = varchar(10)");
	exec(print, "create table bunch_of_stuff foo = varchar(10), bar = real, biz = int, baz = int, qux = varchar(10), good = int");

	// Create a bunch of indexes
	if (print)
		cout << "* Creating indexes on all of the columns" << endl;
	exec(print, "create index n1 on bunch_of_nums");
	exec(print, "create index n1 on bunch_of_nums", false);
	exec(print, "create index nX on bunch_of_nums", false);
	exec(print, "create index n2 on bunch_of_nums");
	exec(print, "create index n3 on bunch_of_nums");
	exec(print, "create index n4 on bunch_of_nums");
	exec(print, "create index n5 on bunch_of_nums");
	exec(print, "create index n6 on bunch_of_nums");
	exec(print, "create index n6 on bunch_of_nums", false);
	exec(print, "create index s1 on bunch_of_strings");
	exec(print, "create index s2 on bunch_of_strings");
	exec(print, "create index s3 on bunch_of_strings");
	exec(print, "create index s4 on bunch_of_strings");
	exec(print, "create index foo on bunch_of_stuff");
	exec(print, "create index bar on bunch_of_stuff");
	exec(print, "create index biz on bunch_of_stuff");
	exec(print, "create index baz on bunch_of_stuff");
	exec(print, "create index qux on bunch_of_stuff");
	exec(print, "create index bad on bunch_of_stuff", false);
	exec(print, "create index good on bunch_of_stuff");

	// Delete SOME of the indexes
	if (print)
		cout << "* Deleting indexes on all but one column from each table" << endl;
	exec(print, "drop index n2 on bunch_of_nums");
	exec(print, "drop index n3 on bunch_of_nums");
	exec(print, "drop index n4 on bunch_of_nums");
	exec(print, "drop index n5 on bunch_of_nums");
	exec(print, "drop index n6 on bunch_of_nums");
	exec(print, "drop index nX on bunch_of_nums", false);
	exec(print, "drop index s2 on bunch_of_strings");
	exec(print, "drop index s3 on bunch_of_strings");
	exec(print, "drop index s4 on bunch_of_strings");
	exec(print, "drop index foo on bunch_of_stuff");
	exec(print, "drop index bar on bunch_of_stuff");
	exec(print, "drop index biz on bunch_of_stuff");
	exec(print, "drop index baz on bunch_of_stuff");
	exec(print, "drop index qux on bunch_of_stuff");
	exec(print, "drop index qux on bunch_of_stuff", false);
	exec(print, "drop index bad on bunch_of_stuff", false);

	// At this point, we should only be left with bunch_of_nums.n1 bunch_of_strings.s1, and bunch_of_stuff.good

	// Delete all of the tables again
	if (print)
		cout << "* Deleting initial tables..." << endl;
	exec(print, "drop table bunch_of_nums");
	exec(print, "drop table bunch_of_strings");
	exec(print, "drop table bunch_of_stuff");

	// Recreate tables with just a single column (the column we didn't delete the index on)
	if (print)
		cout << "* Recreating tables with reduced columns..." << endl;
	exec(print, "create table bunch_of_nums n1 = real");
	exec(print, "create table bunch_of_strings s1 = varchar(11)");
	exec(print, "create table bunch_of_stuff good = int");

	// Make sure the indexes didn't linger
	if (print)
		cout << "* Checking to see indexes didn't survive..." << endl;
	exec(print, "create index n1 on bunch_of_nums");
	exec(print, "create index s1 on bunch_of_strings");
	exec(print, "create index good on bunch_of_stuff");

	// Finally clean up and delete everything
	exec(print, "drop table bunch_of_nums");
	exec(print, "drop table bunch_of_strings");
	exec(print, "drop table bunch_of_stuff");

	exec(print, "drop table bunch_of_nums", false);
	exec(print, "drop table bunch_of_strings", false);
	exec(print, "drop table bunch_of_stuff", false);
}

void VeryLargeIndexTest(bool print)
{
	static const int NUM_ENTRIES = 10;
	if (print)
	{
		cout << "* We will fill up a table with a lot of indexed data and then filter it down to a small result" << endl;
		cout << "* Creating initial tables..." << endl;
	}

	// Create our table and indexes
	static const int MAX_STRING_SIZE = 100;
	exec(print, "create table full_table i = int, r = real, s = varchar(100)");
	exec(print, "create index i on full_table");
	exec(print, "create index r on full_table");
	exec(print, "create index s on full_table");

	// TODO: Add random values
	std::vector<int> iVals;
	std::vector<float> rVals;
	std::vector<std::string> sVals;
	char sBuffer[PAGE_SIZE];

	// Reserve our "special values"
	int iVal, iSpecial = 1234;
	float rVal, rSpecial = 0.999f;
	std::string sVal, sSpecial = "foobar";

	for (int i = 0; i < NUM_ENTRIES; ++i)
	{
		iVal = iSpecial;
		while (iVal == iSpecial)
		{
			iVal = rand();
		}

		rVal = rSpecial;
		while (rVal == rSpecial)
		{
			rVal = rand();
		}

		int sLen = 1 + rand() % MAX_STRING_SIZE;
		memset(sBuffer, 0, PAGE_SIZE);
		for (int sIndex = 0; sIndex < sLen; ++sIndex)
		{
			sBuffer[sIndex] = 'A' + (rand() % 20);
		}
		sSpecial = std::string(sBuffer);


	}

	// TODO: Test current values

	// TODO: Drop indexes and start adding more values

	// TODO: Re-add indexes, and they should be on all of the new values

	// TODO: Test current values

	// Finally clean up by deleting tables
	exec(print, "drop table full_table");
	exec(print, "drop table full_table", false);
}

void cleanup()
{
	remove("RM_SYS_ATTRIBUTE_TABLE.db");
	remove("RM_SYS_CATALOG_TABLE.db");
	remove("RM_SYS_INDEX_TABLE.db");
	remove("cli_columns");
	remove("cli_indexes");
	remove("cli_tables");

	remove("bunch_of_nums");
	remove("bunch_of_strings");
	remove("bunch_of_stuff");
	remove("full_table");
}

int main()
{
	cleanup();
	cli = CLI::Instance();

	RunTest("CreateDelete", CreateDeleteTest, 2);
	RunTest("VeryLargeIndex", VeryLargeIndexTest, 2);

	cleanup();
	return 0;
}
