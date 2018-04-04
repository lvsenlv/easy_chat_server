
all :
	gcc -c ../../src/completion_code.c -o ./completion_code.o -g -Wall -I../../include -D__LINUX -D__64BIT
	gcc -c ../../src/crc.c -o ./crc.o -Wall -D__CRC16 -D__CRC32 -I../../include -D__LINUX -D__64BIT
	gcc -c main.c -g -Wall -I../../include -D__LINUX -D__64BIT -D__CRC32 -D__CRC16
	gcc main.o completion_code.o crc.o -o test

clean :
	rm -f *.o test a.out change_filename
