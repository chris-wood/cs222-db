
include ../makefile.inc

all: librm.a rmtest_1 rmtest_2 rmtest_extra

# lib file dependencies
librm.a: librm.a(rm.o)  # and possibly other .o files
librm.a: librm.a(../util/returncodes.o)
librm.a: librm.a(../util/dbgout.o)

# c file dependencies
rm.o: rm.h ../util/dbgout.h ../util/returncodes.h
rmtest_1.o: rm.h test_util.h ../util/dbgout.h ../util/returncodes.h
rmtest_2.o: rm.h test_util.h ../util/dbgout.h ../util/returncodes.h
rmtest_extra.o: rm.h ../util/dbgout.h ../util/returncodes.h

# binary dependencies
rmtest_1: rmtest_1.o librm.a $(CODEROOT)/rbf/librbf.a
rmtest_2: rmtest_2.o librm.a $(CODEROOT)/rbf/librbf.a
rmtest_extra: rmtest_extra.o librm.a $(CODEROOT)/rbf/librbf.a

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/rbf/librbf.a
$(CODEROOT)/rbf/librbf.a:
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

.PHONY: clean
clean:
	-rm rmtest_1 rmtest_2 rmtest_extra *.a *.o *~
	$(MAKE) -C $(CODEROOT)/rbf clean
