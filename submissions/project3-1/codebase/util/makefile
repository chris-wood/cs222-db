
include ../makefile.inc

all: libutil.a 

# lib file dependencies
libutil.a: libutil.a(dbgout.o)
libutil.a: libutil.a(returncodes.o)
libutil.a: libutil.a(hash.o)

# c file dependencies
dbgout.o: dbgout.h
returncodes.o: returncodes.h
hash.o: hash.h

# binary dependencies

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/util/libutil.a
$(CODEROOT)/util/libutil.a:
	$(MAKE) -C $(CODEROOT)/util libutil.a

.PHONY: clean
clean:
	-rm -f *.a *.o *~
