ifdef CROSS_COMPILE
SYSROOT=$(shell $(CROSS_COMPILE)gcc -print-sysroot)
else
SYSROOT=/
endif 
default: 
	$(CROSS_COMPILE)gcc -g -Wall -c writer.c -o writer.o 
	$(CROSS_COMPILE)gcc -g -Wall -I$(SYSROOT) writer.o -o writer 
clean: 
	rm -f writer.o
	rm -f writer