
include ../makefile.inc

all: libix.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a ixtest1 ixtest2 ix_combined

# lib file dependencies
libix.a: libix.a(ix.o)  # and possibly other .o files
libix.a: libix.a($(CODEROOT)/util/libutil.a)

# c file dependencies
ix.o: ix.h
ixtest1.o: ixtest_util.h
ixtest2.o: ixtest_util.h
ix_combined.o: ixtest_util.h ix.h

# binary dependencies
ixtest1: ixtest1.o libix.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a
ixtest2: ixtest2.o libix.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a
ix_combined: ix_combined.o libix.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a

# dependencies to compile used libraries
$(CODEROOT)/rbf/librbf.a: $(CODEROOT)/util/libutil.a
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

$(CODEROOT)/util/libutil.a: $(CODEROOT)/util/libutil.a
	$(MAKE) -C $(CODEROOT)/util libutil.a

clean:
	-rm -f ixtest1 ixtest2 ix_combined *.a *.o
	$(MAKE) -C $(CODEROOT)/rbf clean
	$(MAKE) -C $(CODEROOT)/util clean
