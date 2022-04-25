EXAMPLES := $(wildcard examples/*)
TESTS := $(filter-out %.h,$(wildcard test/*))
CFLAGS = -g -std=c11 -pedantic -Wall -Wextra -Werror -Wunused -Wswitch-enum

.PHONY: clean run run_examples test

all: cest

cest: cest.c array.h sv.h lexer.h lexer.c
	$(CC) $(CFLAGS) cest.c lexer.c -o cest

.SECONDEXPANSION:
examples: $(EXAMPLES)
$(EXAMPLES): cest $$(patsubst %.h.in,%.h,$$(wildcard $$@/*.h.in)) $$(wildcard $$@/*.c) 
	@echo " -- Making example $(notdir $@) -- "
	$(CC) $(CFLAGS) $(wildcard $@/*.c) -o $@/$(notdir $@)
	@echo
examples/%.h: cest examples/%.h.in
	./cest $@.in $@

tests: $(TESTS)
$(TESTS): $$(patsubst %.c,%.exe,$$(wildcard $$@/*.c))
test/%.exe: lexer.h lexer.c test/%.c
	$(CC) $(CFLAGS) $(patsubst %.exe,%.c,$@) lexer.c -o $@

run: cest
	./cest -h
run_examples: examples
	@for ex in $(foreach ex,$(EXAMPLES),$(notdir $(ex))); do \
		echo " -- Running $$ex --"; \
		examples/$$ex/$$ex; \
		echo; \
	done
test: tests
	@for t in $(foreach t,$(TESTS),$(patsubst %.c,%.exe,$(wildcard $t/*.c))); do \
		$$t && echo "Test $$t ran successfully"; \
	done

spitter: cest.c array.h sv.h lexer.h lexer.c test/spitter.c
	$(CC) $(CFLAGS) test/spitter.c lexer.c -o test/spitter.exe

valgrind: cest
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./cest examples/test/test.h.in -

clean:
	rm -rf cest
	git clean -dXfq examples test
