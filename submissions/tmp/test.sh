#!/bin/bash
unzip $1.zip
cd $1
cd codebase
cd qe
make clean
make
./qetest
