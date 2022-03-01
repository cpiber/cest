.PHONY: run

all: cest

cest: cest.c
	gcc -g -std=c11 -pedantic -Wall -Werror cest.c -o cest

run: cest
	./cest test.c

valgrind: cest
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./cest test.c

clean:
	rm -rf cest
