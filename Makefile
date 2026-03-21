#
# MiLTuX - POSIX-compatible Makefile
#
# Targets:
#   all      - build the miltux binary (default)
#   test     - build and run the test suite
#   clean    - remove build artefacts
#   install  - install miltux to $(PREFIX)/bin
#

# -------------------------------------------------------------------------
# Toolchain: honour CC from environment; fall back to cc (POSIX standard)
# -------------------------------------------------------------------------
CC      ?= cc
PREFIX  ?= /usr/local

# -------------------------------------------------------------------------
# Standard C99 + POSIX 2008; warn aggressively; no platform-specific flags
# -------------------------------------------------------------------------
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200809L \
           -Wall -Wextra -Wpedantic \
           -Wshadow -Wstrict-prototypes -Wmissing-prototypes

LDFLAGS ?=

# -------------------------------------------------------------------------
# Sources and objects
# -------------------------------------------------------------------------
SRCDIR  = src
TESTDIR = tests
BUILDDIR= build

SRCS    = $(SRCDIR)/miltux.c \
          $(SRCDIR)/ring.c   \
          $(SRCDIR)/acl.c    \
          $(SRCDIR)/fs.c     \
          $(SRCDIR)/net.c    \
          $(SRCDIR)/shell.c  \
          $(SRCDIR)/main.c

OBJS    = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Test sources (share all modules except main.c)
TEST_SRCS = $(TESTDIR)/test_miltux.c
LIB_SRCS  = $(filter-out $(SRCDIR)/main.c, $(SRCS))
LIB_OBJS  = $(LIB_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
TEST_BIN  = $(BUILDDIR)/test_miltux

BIN     = $(BUILDDIR)/miltux

# -------------------------------------------------------------------------
# Default target
# -------------------------------------------------------------------------
.PHONY: all
all: $(BIN)

# -------------------------------------------------------------------------
# Link the main binary
# -------------------------------------------------------------------------
$(BIN): $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# -------------------------------------------------------------------------
# Compile each source file
# -------------------------------------------------------------------------
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

# -------------------------------------------------------------------------
# Build directory
# -------------------------------------------------------------------------
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# -------------------------------------------------------------------------
# Test target
# -------------------------------------------------------------------------
.PHONY: test
test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): $(LIB_OBJS) $(BUILDDIR)/test_miltux.o | $(BUILDDIR)
	$(CC) $(CFLAGS) $(LIB_OBJS) $(BUILDDIR)/test_miltux.o -o $@ $(LDFLAGS)

$(BUILDDIR)/test_miltux.o: $(TESTDIR)/test_miltux.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

# -------------------------------------------------------------------------
# Install
# -------------------------------------------------------------------------
.PHONY: install
install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/miltux

# -------------------------------------------------------------------------
# Clean
# -------------------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)
