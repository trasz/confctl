all: confctl

confctl: *.c
	cc -o confctl -Wall -ggdb *.c

clean:
	rm -f *.o confctl
