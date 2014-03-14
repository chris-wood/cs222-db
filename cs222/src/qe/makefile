
include ../makefile.inc

all: libqe.a $(CODEROOT)/ix/libix.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/rm/librm.a $(CODEROOT)/qe/libqe.a $(CODEROOT)/util/libutil.a qetest

# lib file dependencies
libqe.a: libqe.a(qe.o)
libqe.a: libqe.a($(CODEROOT)/util/libutil.a)

# c file dependencies
qe.o: qe.h
qetest.o: qe.h

# binary dependencies
qetest: qetest.o libqe.a $(CODEROOT)/ix/libix.a $(CODEROOT)/rm/librm.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/qe/libqe.a $(CODEROOT)/util/libutil.a 

# dependencies to compile used libraries
$(CODEROOT)/util/libutil.a:
	$(MAKE) -C $(CODEROOT)/util libutil.a

$(CODEROOT)/pf/libpf.a: $(CODEROOT)/util/libutil.a 
	$(MAKE) -C $(CODEROOT)/pf libpf.a

$(CODEROOT)/rbf/librbf.a: $(CODEROOT)/util/libutil.a 
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

$(CODEROOT)/rm/librm.a: $(CODEROOT)/util/libutil.a 
	$(MAKE) -C $(CODEROOT)/rm librm.a

$(CODEROOT)/ix/libix.a: $(CODEROOT)/util/libutil.a 
	$(MAKE) -C $(CODEROOT)/ix libix.a

clean:
	-rm qetest *.a *.o *~
	$(MAKE) -C $(CODEROOT)/rm clean
	$(MAKE) -C $(CODEROOT)/ix clean 
	$(MAKE) -C $(CODEROOT)/rbf clean
	$(MAKE) -C $(CODEROOT)/util clean