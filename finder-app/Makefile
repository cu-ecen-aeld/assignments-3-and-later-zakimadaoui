ifdef CC
SYSROOT=$(shell $(CC) -print-sysroot)
else
SYSROOT=/
endif
all: default

default: 
	$(CC) -g -Wall -c writer.c -o writer.o 
	$(CC) -g -Wall -I$(SYSROOT) writer.o -o writer 

clean: 
	rm -f writer.o
	rm -f writer