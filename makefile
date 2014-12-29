BLDFLAGS                  = -Wall -Wextra -pedantic -std=c99
COMMONFLAGS               = -D__STDC_CONSTANT_MACROS -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE=1
CFLAGS                    = -O3
CDFLAGS                   = -v -DDEBUG=1 -g -O0 -fno-inline
CPFLAGS                   = -pg
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
	$(CC) $(BLDFLAGS) $(COMMONFLAGS) $(CFLAGS) -c $(SOURCE) -o $(OBJDIR)/$(PROG).o $(INCLUDES)
	$(CC) $(BLDFLAGS) $(COMMONFLAGS) $(CFLAGS) $(OBJDIR)/$(PROG).o -o $(PROG) $(LIBS)

debug: setup
	$(CC) $(BLDFLAGS) $(COMMONFLAGS) $(CDFLAGS) -c $(SOURCE) -o $(OBJDIR)/$(PROG).o $(INCLUDES)
	$(CC) $(BLDFLAGS) $(COMMONFLAGS) $(CDFLAGS) $(OBJDIR)/$(PROG).o -o $(PROG) $(LIBS)

profile: setup
	$(CC) -shared -fPIC gprof-helper.c -o gprof-helper.so $(LIBS) -ldl
	$(CC) $(BLDFLAGS) $(COMMONFLAGS) $(CPFLAGS) -c $(SOURCE) -o $(OBJDIR)/$(PROG).o $(INCLUDES)
	$(CC) $(BLDFLAGS) $(COMMONFLAGS) $(CPFLAGS) $(OBJDIR)/$(PROG).o -o $(PROG) $(LIBS)
	@echo "\nNote: To profile convert2bed with gprof/pthreads, run:\n\t$$ LD_PRELOAD=/path/to/gprof-helper.so convert2bed"

install:
	cp -f $(PROG) /usr/local/bin
	cp -f $(WRAPPERDIR)/* /usr/local/bin

clean:
	rm -f $(PROG)
	rm -rf $(OBJDIR)
	rm -rf  *.so *~