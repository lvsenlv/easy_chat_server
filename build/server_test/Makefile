
all :
	gcc -c ../../src/completion_code.c -o ./completion_code.o -g -Wall -I../../include -D__LINUX -D__64BIT
	gcc -c main.c -g -Wall -I../../include -D__LINUX -D__64BIT
	gcc main.o completion_code.o -o test

clean :
	rm -f *.o test a.out change_filename
