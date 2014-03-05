
include ../makefile.inc

all: libqe.a $(CODEROOT)/util/libutil.a $(CODEROOT)/ix/libix.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/rm/librm.a qetest

# lib file dependencies
libqe.a: libqe.a(qe.o)

# c file dependencies
qe.o: qe.h
qetest.o: qe.h

# binary dependencies
qetest: qetest.o libqe.a $(CODEROOT)/util/libutil.a $(CODEROOT)/ix/libix.a $(CODEROOT)/rm/librm.a $(CODEROOT)/rbf/librbf.a

# dependencies to compile used libraries
.PHONY: $(CODEROOT)/util/libutil.a
$(CODEROOT)/util/libutil.a:
	$(MAKE) -C $(CODEROOT)/util libutil.a

.PHONY: $(CODEROOT)/rbf/librbf.a
$(CODEROOT)/rbf/librbf.a:
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

.PHONY: $(CODEROOT)/rm/librm.a
$(CODEROOT)/rm/librm.a:
	$(MAKE) -C $(CODEROOT)/rm librm.a

.PHONY: $(CODEROOT)/ix/libix.a
$(CODEROOT)/ix/libix.a:
	$(MAKE) -C $(CODEROOT)/ix libix.a

.PHONY: clean
clean:
	-rm qetest *.a *.o *~
	$(MAKE) -C $(CODEROOT)/rm clean
	$(MAKE) -C $(CODEROOT)/ix clean 
	$(MAKE) -C $(CODEROOT)/rbf clean
	$(MAKE) -C $(CODEROOT)/util clean