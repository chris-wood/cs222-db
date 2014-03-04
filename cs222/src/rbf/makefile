
include ../makefile.inc

all: $(CODEROOT)/util/libutil.a librbf.a rbftest

# lib file dependencies
librbf.a: librbf.a(pfm.o)
librbf.a: librbf.a(rbcm.o)
librbf.a: librbf.a(rbfm.o)

# c file dependencies
pfm.o: pfm.h
rbcm.o: rbcm.h
rbfm.o: rbfm.h
rbftest.o: pfm.h rbcm.h rbfm.h

# binary dependencies
rbftest: rbftest.o librbf.a $(CODEROOT)/util/libutil.a

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/rbf/librbf.a
$(CODEROOT)/rbf/librbf.a:
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

.PHONY: $(CODEROOT)/util/libutil.a
$(CODEROOT)/util/libutil.a:
	$(MAKE) -C $(CODEROOT)/util libutil.a

.PHONY: clean
clean:
	-rm -f rbftest *.a *.o *~
	 $(MAKE) -C $(CODEROOT)/util clean
