# Makefile for Clumsy Language Compiler

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
TARGET = clumsyc
SRCDIR = src
TESTDIR = tests
TMPDIR = tmp
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/tokenizer.c $(SRCDIR)/parser.c $(SRCDIR)/compiler.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(SRCDIR)/types.h $(SRCDIR)/tokenizer.h $(SRCDIR)/parser.h $(SRCDIR)/compiler.h

# Test files
TESTS = $(wildcard $(TESTDIR)/*.cpl)

.PHONY: all clean test install help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	rm -f *.s *.out test_output
	rm -rf $(TMPDIR)

test: $(TARGET)
	@echo "Running compiler tests..."
	@mkdir -p $(TMPDIR)
	@failed=0; total=0; \
	for test in $(TESTS); do \
		total=$$((total + 1)); \
		testname=$$(basename $$test .cpl); \
		echo -n "Testing $$testname... "; \
		\
		if ./$(TARGET) $$test > $(TMPDIR)/$$testname.s 2>/dev/null; then \
			if as -o $(TMPDIR)/$$testname.o $(TMPDIR)/$$testname.s 2>/dev/null && \
			   ld -o $(TMPDIR)/$$testname.x $(TMPDIR)/$$testname.o -lSystem \
			      -syslibroot `xcrun -sdk macosx --show-sdk-path` -e _main 2>/dev/null; then \
				if [ -f $(TESTDIR)/$$testname.expected ]; then \
					$(TMPDIR)/$$testname.x > $(TMPDIR)/$$testname.actual 2>/dev/null; \
					if diff -q $(TESTDIR)/$$testname.expected $(TMPDIR)/$$testname.actual >/dev/null 2>&1; then \
						echo "PASS"; \
					else \
						echo "FAIL (output mismatch)"; \
						failed=$$((failed + 1)); \
					fi; \
				elif [ -f $(TESTDIR)/$$testname.expected_exit_code ]; then \
					$(TMPDIR)/$$testname.x >/dev/null 2>&1; \
					actual_exit_code=$$?; \
					expected_exit_code=$$(cat $(TESTDIR)/$$testname.expected_exit_code); \
					if [ $$actual_exit_code -eq $$expected_exit_code ]; then \
						echo "PASS"; \
					else \
						echo "FAIL (exit code mismatch: expected $$expected_exit_code, got $$actual_exit_code)"; \
						failed=$$((failed + 1)); \
					fi; \
				else \
					if $(TMPDIR)/$$testname.x >/dev/null 2>&1; then \
						echo "PASS"; \
					else \
						echo "FAIL (runtime error)"; \
						failed=$$((failed + 1)); \
					fi; \
				fi; \
			else \
				echo "FAIL (link error)"; \
				failed=$$((failed + 1)); \
			fi; \
		else \
			echo "FAIL (compile error)"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "Results: $$((total - failed))/$$total tests passed"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

test-verbose: $(TARGET)
	@echo "Running compiler tests (verbose)..."
	@for test in $(TESTS); do \
		echo "=== Testing $$test ==="; \
		./$(TARGET) $$test; \
		echo; \
	done

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

help:
	@echo "Available targets:"
	@echo "  all          - Build the compiler"
	@echo "  clean        - Remove build artifacts"
	@echo "  test         - Run all tests (summary output)"
	@echo "  test-verbose - Run all tests (show assembly output)"
	@echo "  install      - Install to /usr/local/bin"
	@echo "  help         - Show this help"