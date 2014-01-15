## For students: change this path to the root of your code
CODEROOT = "../"
BOOSTROOT = "../../inc/boost_1_55_0/"

#CC = gcc
CC = g++

#CPPFLAGS = -Wall -I$(CODEROOT) -I$(BOOSTROOT) -O3  # maximal optimization
CPPFLAGS = -Wall -I$(CODEROOT) -I$(BOOSTROOT)  -g  # with debugging info
