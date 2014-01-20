#!/bin/sh

cd ../src/rbf/
rm *.gcda
rm *.gcno
rm out.txt
make clean

make --makefile=makefile_coverage
./rbftest > out.txt

lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory ./../../coverage

make clean
rm *.gcda
rm *.gcno
rm out.txt
rm coverage.info
