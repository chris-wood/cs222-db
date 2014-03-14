
include ../makefile.inc

all: librbf.a $(CODEROOT)/util/libutil.a rbftest

# lib file dependencies
librbf.a: librbf.a(pfm.o)
librbf.a: librbf.a(rbcm.o)
librbf.a: librbf.a(rbfm.o)
librbf.a: librbf.a($(CODEROOT)/util/libutil.a)

# c file dependencies
pfm.o: pfm.h
rbcm.o: rbcm.h
rbfm.o: rbfm.h
rbftest.o: pfm.h rbcm.h rbfm.h

# binary dependencies
rbftest: rbftest.o librbf.a $(CODEROOT)/util/libutil.a

# dependencies to compile used libraries
$(CODEROOT)/rbf/librbf.a: $(CODEROOT)/util/libutil.a 
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

$(CODEROOT)/util/libutil.a:
	$(MAKE) -C $(CODEROOT)/util libutil.a

clean:
	-rm -f rbftest *.a *.o *~
	 $(MAKE) -C $(CODEROOT)/util clean
