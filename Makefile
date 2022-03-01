.PHONY: run

all: cest

cest: cest.c
	gcc -std=c11 -pedantic -Wall -Werror cest.c -o cest

run: cest
	./cest test.c

clean:
	rm -rf cest
