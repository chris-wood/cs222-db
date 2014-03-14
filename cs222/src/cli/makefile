
include ../makefile.inc

#CXXFLAGS =-std=c++11

all: libcli.a $(CODEROOT)/util/libutil.a clitest start 

# lib file dependencies
libcli.a: libcli.a(cli.o)  # and possibly other .o files
libcli.a: libcli.a($(CODEROOT)/util/libutil.a)

# c file dependencies
cli.o: cli.h $(CODEROOT)/util/libutil.a
clitest.o: cli.h $(CODEROOT)/util/libutil.a
start.o: cli.h $(CODEROOT)/util/libutil.a

# binary dependencies
clitest: clitest.o libcli.a $(CODEROOT)/qe/libqe.a $(CODEROOT)/ix/libix.a $(CODEROOT)/rm/librm.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a 
start: start.o libcli.a $(CODEROOT)/qe/libqe.a $(CODEROOT)/ix/libix.a $(CODEROOT)/rm/librm.a $(CODEROOT)/rbf/librbf.a $(CODEROOT)/util/libutil.a 

# dependencies to compile used libraries
$(CODEROOT)/util/libutil.a:
	$(MAKE) -C $(CODEROOT)/util libutil.a

$(CODEROOT)/rm/librm.a: $(CODEROOT)/util/libutil.a
	$(MAKE) -C $(CODEROOT)/rm librm.a

$(CODEROOT)/rbf/librbf.a: $(CODEROOT)/util/libutil.a
	$(MAKE) -C $(CODEROOT)/rbf librbf.a

$(CODEROOT)/ix/libix.a: $(CODEROOT)/rbf/librbf.a $(CODEROOT)/rm/librm.a $(CODEROOT)/util/libutil.a
	$(MAKE) -C $(CODEROOT)/ix libix.a

$(CODEROOT)/qe/libqe.a: $(CODEROOT)/pf/libpf.a $(CODEROOT)/rm/librm.a $(CODEROOT)/ix/libix.a $(CODEROOT)/util/libutil.a
	$(MAKE) -C $(CODEROOT)/qe libqe.a

$(CODEROOT)/pf/libpf.a: $(CODEROOT)/util/libutil.a
	$(MAKE) -C $(CODEROOT)/pf libpf.a

clean:
	-rm clitest start *.a *.o *~
	$(MAKE) -C $(CODEROOT)/rbf clean
	$(MAKE) -C $(CODEROOT)/rm clean
	$(MAKE) -C $(CODEROOT)/ix clean	
	$(MAKE) -C $(CODEROOT)/qe clean	
	$(MAKE) -C $(CODEROOT)/util clean
