BLDFLAGS                  = -Wall -Wextra -pedantic -std=c99
CFLAGS                    = -D__STDC_CONSTANT_MACROS -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE=1 -O3
CDFLAGS                   = -D__STDC_CONSTANT_MACROS -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE=1 -DDEBUG=1 -g -O0 -fno-inline
CPFLAGS                   = -D__STDC_CONSTANT_MACROS -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE=1 -DDEBUG=1 -g -pg
LIBS                      = -lpthread
INCLUDES                 := -iquote"${PWD}"
OBJDIR                    = objects
WRAPPERDIR                = wrappers
PROG                      = convert2bed
SOURCE                    = convert2bed.c

all: setup build

.PHONY: setup build debug profile clean

setup:
	mkdir -p $(OBJDIR)

build: setup
	$(CC) $(BLDFLAGS) $(CFLAGS) -c $(SOURCE) -o $(OBJDIR)/$(PROG).o $(INCLUDES)
	$(CC) $(BLDFLAGS) $(CFLAGS) $(OBJDIR)/$(PROG).o -o $(PROG) $(LIBS)

debug: setup
	$(CC) $(BLDFLAGS) $(CDFLAGS) -c $(SOURCE) -o $(OBJDIR)/$(PROG).o $(INCLUDES)
	$(CC) $(BLDFLAGS) $(CDFLAGS) $(OBJDIR)/$(PROG).o -o $(PROG) $(LIBS)

profile: setup
	$(CC) $(BLDFLAGS) $(CPFLAGS) -c $(SOURCE) -o $(OBJDIR)/$(PROG).o $(INCLUDES)
	$(CC) $(BLDFLAGS) $(CPFLAGS) $(OBJDIR)/$(PROG).o -o $(PROG) $(LIBS)

install:
	cp -f $(PROG) /usr/local/bin
	cp -f $(WRAPPERDIR)/* /usr/local/bin

clean:
	rm -f $(PROG)
	rm -rf $(OBJDIR)
	rm -rf  *.so *~