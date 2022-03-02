.PHONY: run

all: cest

cest: cest.c array.h sv.h
	gcc -g -std=c11 -pedantic -Wall -Wextra -Werror -Wunused cest.c -o cest

run: cest
	./cest test.h.in test.h

valgrind: cest
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./cest test.c

clean:
	rm -rf cest
