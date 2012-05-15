all: confctl

confctl: *.c
	clang -o confctl -Wall -pedantic -ggdb *.c

clean:
	rm -rf *.o confctl *.dSYM
