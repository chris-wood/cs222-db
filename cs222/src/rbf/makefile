
include ../makefile.inc

all: librbf.a rbftest

# lib file dependencies
librbf.a: librbf.a(pfm.o)  # and possibly other .o files
librbf.a: librbf.a(rbfm.o)
librbf.a: librbf.a(returncodes.o)

# c file dependencies
pfm.o: pfm.h returncodes.h
rbfm.o: rbfm.h returncodes.h
returncodes.o: returncodes.h

rbftest.o: pfm.h rbfm.h returncodes.h

# binary dependencies
rbftest: rbftest.o librbf.a $(CODEROOT)/rbf/librbf.a

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/rbf/librbf.a
$(CODEROOT)/rbf/librbf.a:
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

.PHONY: clean
clean:
	-rm rbftest *.a *.o *~
