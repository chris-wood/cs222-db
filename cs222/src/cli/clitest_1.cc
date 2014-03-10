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

void RepeatTest(const std::string& prefix, fn_test test, int numRetries)
{
	cout << prefix;

	if (numRetries < 1)
	{
		cout << "Not running test, numRetries=" << numRetries << endl;
	}
	else
	{
		test(true);
	}
	
	cout << "\n=========================================" << endl;
	cout << "Repeating test " << (numRetries-1) << " times:";
	for (int i=1; i<numRetries; ++i)
	{
		cout << ".";
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
	if (print)
	{
		exec(print, "print cli_columns");
		exec(print, "print cli_tables");
	}

	// Delete all of the tables
	if (print)
		cout << "* Deleting initial tables..." << endl;
	exec(print, "drop table bunch_of_nums");
	exec(print, "drop table bunch_of_strings");
	exec(print, "drop table bunch_of_stuff");
	if (print)
	{
		exec(print, "print cli_columns");
		exec(print, "print cli_tables");
	}

	// Recreate the tables we had before, but with different columns
	if (print)
		cout << "* Recreating tables with different columns..." << endl;
	exec(print, "create table bunch_of_nums n1 = real, n2 = real, n3 = int, n4 = int, n5 = int, n6 = int");
	exec(print, "create table bunch_of_strings s1 = varchar(11), s2 = varchar(100), s3 = varchar(4), s4 = varchar(10)");
	exec(print, "create table bunch_of_stuff foo = varchar(10), bar = real, biz = int, baz = int, qux = varchar(10), bad = int, good = int");
	if (print)
	{
		exec(print, "print cli_columns");
		exec(print, "print cli_tables");
	}

	// Create a bunch of indexes

	// Delete SOME of the indexes

	// Delete all of the tables again
	if (print)
		cout << "* Deleting initial tables..." << endl;
	exec(print, "drop table bunch_of_nums");
	exec(print, "drop table bunch_of_strings");
	exec(print, "drop table bunch_of_stuff");
	if (print)
	{
		exec(print, "print cli_columns");
		exec(print, "print cli_tables");
	}
}

void CreateDeleteTest(int numRetries)
{
	RepeatTest("*********** CLI BasicTest begins ******************", CreateDeleteTest, numRetries);
}

void cleanup()
{
	remove("RM_SYS_ATTRIBUTE_TABLE.db");
	remove("RM_SYS_CATALOG_TABLE.db");
	remove("RM_SYS_INDEX_TABLE.db");
	remove("cli_columns");
	remove("cli_indexes");
	remove("cli_tables");

	remove("tbl_employee");
}

int main()
{
	cleanup();
	cli = CLI::Instance();

	CreateDeleteTest(64);

	cleanup();
	return 0;
}
