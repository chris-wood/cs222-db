
include ../makefile.inc

all: librbf.a rbftest

# lib file dependencies
librbf.a: librbf.a(pfm.o)
librbf.a: librbf.a(rbcm.o)
librbf.a: librbf.a(rbfm.o)
librbf.a: librbf.a(../util/dbgout.o)
librbf.a: librbf.a(../util/returncodes.o)

# c file dependencies
pfm.o: pfm.h ../util/returncodes.h ../util/dbgout.h
rbcm.o: rbcm.h  ../util/returncodes.h ../util/dbgout.h
rbfm.o: rbfm.h  ../util/returncodes.h ../util/dbgout.h
../util/returncodes.o: ../util/returncodes.h
../util/dbgout.o: ../util/dbgout.h

rbftest.o: pfm.h rbcm.h rbfm.h ../util/returncodes.h ../util/dbgout.h

# binary dependencies
rbftest: rbftest.o librbf.a $(CODEROOT)/rbf/librbf.a

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/rbf/librbf.a
$(CODEROOT)/rbf/librbf.a:
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

.PHONY: clean
clean:
	-rm rbftest *.a *.o *~
