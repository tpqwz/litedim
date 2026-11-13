# One self-contained executable. The X client libraries are linked in.
# The binary still needs a running X server and a compositor.
# The feature macro stays on the command line so the source does not define it.
# GTK stays out of this link. The tray and the settings window are in this file.

CC      ?= cc
CFLAGS  ?= -std=c11 -Os -Wall -Wextra
CPPFLAGS ?= -D_GNU_SOURCE
LDFLAGS ?= -static
LIBS    := $(shell pkg-config --static --libs xrender xfixes xext x11)
CITE    ?= /home/yy/code/offside/cite/build/cite
SRCS    := src/litedim.c src/decide.c

.PHONY: all clean check

all: litedim

litedim: $(SRCS) src/decide.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS) $(LIBS)
	strip $@

check: $(SRCS) src/decide.h check/decide_test.c check/decide_eva.c
	$(CITE) src
	frama-c -machdep x86_64 -eva -eva-precision 2 -main eva_main \
		-cpp-extra-args="-std=c11 -Isrc" check/decide_eva.c src/decide.c \
		> check/eva.log
	@if grep -q 'eva:alarm' check/eva.log; then \
		echo "eva alarms:"; \
		grep 'eva:alarm' check/eva.log; \
		exit 1; \
	fi
	@grep -E 'alarms generated|Assertions' check/eva.log
	$(CC) -std=c11 -Wall -Wextra -Werror -Isrc -o check/decide_test check/decide_test.c src/decide.c
	./check/decide_test

clean:
	rm -f litedim check/decide_test
