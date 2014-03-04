
include ../makefile.inc

all: librm.a $(CODEROOT)/util/libutil.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/ix/libix.a rmtest_1 rmtest_2 rmtest_extra combined_tests

# lib file dependencies
librm.a: librm.a(rm.o)

# c file dependencies
rm.o: rm.h
rmtest_1.o: rm.h test_util.h
rmtest_2.o: rm.h test_util.h
rmtest_extra.o: rm.h
combined_tests.o: rm.h test_util.h

# binary dependencies
rmtest_1: rmtest_1.o librm.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a $(CODEROOT)/ix/libix.a
rmtest_2: rmtest_2.o librm.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a $(CODEROOT)/ix/libix.a
rmtest_extra: rmtest_extra.o librm.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a $(CODEROOT)/ix/libix.a
combined_tests: combined_tests.o librm.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a $(CODEROOT)/ix/libix.a

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/rbf/librbf.a 
$(CODEROOT)/rbf/librbf.a:
	$(MAKE) -C $(CODEROOT)/rbf librbf.a
	
.PHONY: $(CODEROOT)/ix/libix.a 
$(CODEROOT)/ix/libix.a:
	$(MAKE) -C $(CODEROOT)/rbf libix.a

.PHONY: $(CODEROOT)/util/libutil.a	
$(CODEROOT)/util/libutil.a:
	$(MAKE) -C $(CODEROOT)/util libutil.a

.PHONY: clean
clean:
	-rm -f rmtest_1 rmtest_2 rmtest_extra combined_tests *.a *.o *~
	$(MAKE) -C $(CODEROOT)/rbf clean
	$(MAKE) -C $(CODEROOT)/util clean
	$(MAKE) -C $(CODEROOT)/ix  clean
