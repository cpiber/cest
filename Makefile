EXAMPLES := $(wildcard examples/*)
CFLAGS = -g -std=c11 -pedantic -Wall -Wextra -Werror -Wunused -Wswitch-enum

.PHONY: clean run run_examples

all: cest

cest: cest.c array.h sv.h lexer.c
	$(CC) $(CFLAGS) cest.c lexer.c -o cest

.SECONDEXPANSION:
examples: $(EXAMPLES)
$(EXAMPLES): cest $$(patsubst %.h.in,%.h,$$(wildcard $$@/*.h.in)) $$(wildcard $$@/*.c) 
	@echo " -- Making example $(notdir $@) -- "
	$(CC) $(CFLAGS) $(wildcard $@/*.c) -o $@/$(notdir $@)
	@echo
examples/%.h: cest examples/%.h.in
	./cest $@.in $@

run: cest
	./cest -h
run_examples: examples
	@for ex in $(foreach ex,$(EXAMPLES),$(notdir $(ex))); do \
		echo " -- Running $$ex --"; \
		examples/$$ex/$$ex; \
		echo; \
	done

valgrind: cest
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s ./cest examples/test/test.h.in -

clean:
	rm -rf cest
	git clean -dXfq examples
