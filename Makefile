all: confctl

confctl: *.c
	cc -o confctl -Wall -pedantic -ggdb *.c

clean:
	rm -rf *.o confctl *.dSYM
