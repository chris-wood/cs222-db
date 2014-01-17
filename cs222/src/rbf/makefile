
include ../makefile.inc

all: librbf.a rbftest

# lib file dependencies
librbf.a: librbf.a(pfm.o)
librbf.a: librbf.a(rbfm.o)
librbf.a: librbf.a(dbgout.o)
librbf.a: librbf.a(returncodes.o)

# c file dependencies
pfm.o: pfm.h returncodes.hdbgout.h
rbfm.o: rbfm.h returncodes.h dbgout.h
returncodes.o: returncodes.h
dbgout.c: dbgout.h

rbftest.o: pfm.h rbfm.h returncodes.h dbgout.h

# binary dependencies
rbftest: rbftest.o librbf.a $(CODEROOT)/rbf/librbf.a

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/rbf/librbf.a
$(CODEROOT)/rbf/librbf.a:
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

.PHONY: clean
clean:
	-rm rbftest *.a *.o *~
