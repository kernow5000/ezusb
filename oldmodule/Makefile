obj-m += flashadvance.o

default: clean
	 make -C /lib/modules/$(shell uname -r)/build/ SUBDIRS=$(PWD) modules
       	
clean:	 
	 rm *~ *.o *.ko *.mod.c -fr
	
      
