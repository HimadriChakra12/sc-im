# Specify the name of the resulting executable file
name = xc

# The base directory where everything should be installed.
prefix  = /usr/local

EXDIR   = $(prefix)/bin
HELPDIR = $(prefix)/share/$(name)
THEMEDIR = $(prefix)/share/themes
LIBDIR  = $(prefix)/share/doc/$(name)

# This is where the man page goes.
MANDIR  = $(prefix)/share/man/man1

# History directory, relative to $HOME
HISTORY_DIR= .cache
HISTORY_FILE=xcinfo
# Configuration directory, relative to $HOME
CONFIG_DIR= .config/xc
CONFIG_FILE=scimrc

# Change these to your liking or use `make CC=gcc` etc
CC   := clang
YACC := bison -y
#SED  = sed

# ---------------------------------------------------------------------------
# Speed: use ccache when available, and build in parallel by default.
# Override with `make -j1` if you want serial output for debugging.
# ---------------------------------------------------------------------------
ifneq (, $(shell which ccache 2>/dev/null))
  CC := ccache $(CC)
endif

NPROC      := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
MAKEFLAGS  += -j$(NPROC) -Otarget --no-print-directory

LDLIBS += -lm

CFLAGS += -Wall -g
CFLAGS += -DNCURSES
CFLAGS += -D_XOPEN_SOURCE_EXTENDED -D_GNU_SOURCE
CFLAGS += -DSNAME=\"$(name)\"
CFLAGS += -DHELP_PATH=\"$(HELPDIR)\"
CFLAGS += -DLIBDIR=\"$(LIBDIR)\"

# Sets default pager, e.g. 'less' or 'more'
CFLAGS += -DDFLT_PAGER=\"less\"
# Sets default editor. Its use in case EDITOR env variable is not set
CFLAGS += -DDFLT_EDITOR=\"vim\"
# Comment out to disable color support
CFLAGS += -DUSECOLORS
# Command history file, relative to HISTORY_DIR directory. Comment out to disable commandline history
CFLAGS += -DHISTORY_FILE=\"$(HISTORY_FILE)\" -DHISTORY_DIR=\"$(HISTORY_DIR)\"
# Configuration file, relative to CONFIG_DIR directory
CFLAGS += -DCONFIG_FILE=\"$(CONFIG_FILE)\" -DCONFIG_DIR=\"$(CONFIG_DIR)\"
# Input mode history. Same as previous, but for insert mode commands
CFLAGS += -DINS_HISTORY_FILE=\"xcinfo\"
# Comment out to disable undo/redo support
CFLAGS += -DUNDO
# Maximum number of rows in spreadsheet. Up to 1048576
CFLAGS += -DMAXROWS=65536
# Used for date formatting with C-d shortcut using you local d_fmt
CFLAGS += -DUSELOCALE
# Comment out to enable mouse support on virtual terminal.
CFLAGS += -DMOUSE

# Auto-dependency tracking: only recompile .c files whose headers actually
# changed instead of relying on the coarse "$(OBJS): y.tab.h ..." rule below.
CFLAGS += -MMD -MP

# Clipboard support is OS dependent.
#
# Choose one of the following commands for copying to different clipboards:
# You can later change it at runtime.
#to copy to tmux clipboard:
#CFLAGS += -DDEFAULT_COPY_TO_CLIPBOARD_CMD=\""tmux load-buffer"\"
#to copy to X clipboard:
CFLAGS += -DDEFAULT_COPY_TO_CLIPBOARD_CMD=\""xclip -i -selection clipboard <"\"
#to copy to OSX clipboard:
#CFLAGS += -DDEFAULT_COPY_TO_CLIPBOARD_CMD=\""pbcopy <"\"
#
# Choose one of the proposed commands for pasting from different clipboards:
# You can later change it at runtime.
#CFLAGS += -DDEFAULT_PASTE_FROM_CLIPBOARD_CMD=\""tmux show-buffer"\"
CFLAGS += -DDEFAULT_PASTE_FROM_CLIPBOARD_CMD=\""xclip -o -selection clipboard"\"
#CFLAGS += -DDEFAULT_PASTE_FROM_CLIPBOARD_CMD=\""pbpaste"\"

# Command to open file or link under cursor
CFLAGS += -DDEFAULT_OPEN_FILE_UNDER_CURSOR_CMD=\""scopen"\"

# Autobackup. If you unset this, no backup check nor autobackup feature will be available.
CFLAGS += -DAUTOBACKUP
# Have threads? Set these two, if you want the autobackup feature to work with threads.
CFLAGS += -DHAVE_PTHREAD

ifneq ($(shell uname -s),Darwin)
  LDLIBS += -pthread
endif

# Check for gnuplot existence
ifneq (, $(shell which gnuplot))
  CFLAGS += -DGNUPLOT
endif

# OpenBSD does not implement wordexp()
ifeq ($(shell uname -s),OpenBSD)
  CFLAGS += -DNO_WORDEXP
endif

# dynamic linking (not available in BSD)
ifneq ($(shell uname -s | grep -o BSD),BSD)
  LDLIBS += -ldl
endif

ifneq (, $(shell which pkg-config))
  # Any system with pkg-config

  # NOTE: ncursesw (required)
  ifneq ($(shell pkg-config --exists ncursesw || echo 'no'),no)
    CFLAGS += $(shell pkg-config --cflags ncursesw)
    LDLIBS += $(shell pkg-config --libs ncursesw)
  else ifneq ($(shell pkg-config --exists ncurses || echo 'no'),no)
    # hopefully this includes wide character support then
    CFLAGS += $(shell pkg-config --cflags ncurses)
    LDLIBS += $(shell pkg-config --libs ncurses)
  else
    LDLIBS += -lncursesw
  endif

  # NOTE: libxlsreader (libxls) is required for xls file reading support
  ifneq ($(shell pkg-config --exists libxls || echo 'no'),no)
    CFLAGS += -DXLS $(shell pkg-config --cflags libxls)
    LDLIBS += $(shell pkg-config --libs libxls)
  endif

  # NOTE: libxml and libzip are required for xlsx file import support
  # xc (this fork) is Excel-only: only XLSX/XLS are supported file formats.
  ifneq ($(shell pkg-config --exists libzip libxml-2.0 || echo 'no'),no)
    CFLAGS += -DXLSX $(shell pkg-config --cflags libxml-2.0 libzip)
    LDLIBS += $(shell pkg-config --libs libxml-2.0 libzip)
  endif

  # NOTE: libxlsxwriter is required for xlsx file export support
  ifneq ($(shell pkg-config --exists xlsxwriter || echo 'no'),no)
    CFLAGS += -DXLSX_EXPORT $(shell pkg-config --cflags xlsxwriter)
    LDLIBS += $(shell pkg-config --libs xlsxwriter)
  endif

  # NOTE: lua support
  LUA_PKGNAME ?= $(shell pkg-config --list-all | awk '/^lua-?[0-9.]+[[:space:]]/ { p=$$1; gsub("[^[0-9]", "", p); print p " " $$1; }' | LC_ALL=C sort -nrk1 | uniq | head -n 1 | awk '{print $$2}')
  ifneq ($(LUA_PKGNAME),)
    CFLAGS += -DXLUA $(shell pkg-config --cflags $(LUA_PKGNAME))
    ifneq ($(shell uname -s),Darwin)
      LDLIBS += $(shell pkg-config --libs $(LUA_PKGNAME)) -Wl,--export-dynamic
    else
      LDLIBS += $(shell pkg-config --libs $(LUA_PKGNAME)) -rdynamic
    endif
  else ifneq ($(shell pkg-config --exists luajit || echo 'no'),no) # If not found, check for luajit
    CFLAGS += -DXLUA $(shell pkg-config --cflags luajit)
    ifneq ($(shell uname -s),Darwin)
      LDLIBS += $(shell pkg-config --libs luajit) -Wl,--export-dynamic
    else
      LDLIBS += $(shell pkg-config --libs luajit) -rdynamic
    endif
  endif
else ifeq ($(shell uname -s),NetBSD)
  # NetBSD without pkg-config

  CFLAGS  += -I/usr/pkg/include
  CFLAGS  += -I/usr/pkg/include/ncursesw

  LDFLAGS += -L/usr/pkg/lib
  LDFLAGS += -Wl,-R/usr/pkg/lib

  LDLIBS += -lncursesw
else
  LDFLAGS += -lncursesw
endif

SRCS = $(filter-out gram.c, $(wildcard *.c)) $(wildcard actions/*.c) $(wildcard formats/*.c) $(wildcard cmds/*.c) $(wildcard utils/*.c)
OBJS = $(patsubst %.c, %.o, $(SRCS)) gram.o
DEPS = $(OBJS:.o=.d)

# ---------------------------------------------------------------------------
# Pretty, timed build output (sxbv-style)
# ---------------------------------------------------------------------------
BOLD   = \033[1m
DIM    = \033[2m
CYAN   = \033[1;36m
GREEN  = \033[1;32m
ORANGE = \033[38;5;208m
RESET  = \033[0m

COUNT_FILE  := /tmp/.$(name)-build-count
LOCK_FILE   := /tmp/.$(name)-build.lock
TOTAL_OBJS  := $(words $(OBJS))
CTR_W       := $(shell printf "%s" "$(TOTAL_OBJS)" | wc -c | tr -d ' ')

# Reset the counter once per invocation, at parse time, before any recipe runs.
$(shell rm -f $(COUNT_FILE))

define compile_c
	@t0=$$(date +%s%N); \
	out=$$($(CC) $(CFLAGS) -c $< -o $@ 2>&1); rc=$$?; \
	t1=$$(date +%s%N); ms=$$(( (t1 - t0) / 1000000 )); \
	( \
		flock -w 10 200; \
		n=$$(( $$(cat $(COUNT_FILE) 2>/dev/null || echo 0) + 1 )); \
		printf "%d" $$n > $(COUNT_FILE); \
		if [ $$rc -eq 0 ]; then \
			printf "$(DIM)[%$(CTR_W)d/$(TOTAL_OBJS)]$(RESET) $(GREEN)%-8s$(RESET) $(BOLD)%-40s$(RESET) -> %-40s\t$(CYAN)%dms$(RESET)\n" \
				"$$n" "$(CC)" "$<" "$@" "$$ms"; \
		else \
			printf "$(DIM)[%$(CTR_W)d/$(TOTAL_OBJS)]$(RESET) $(ORANGE)%-8s$(RESET) $(BOLD)%-40s$(RESET) FAILED\t$(CYAN)%dms$(RESET)\n" \
				"$$n" "$(CC)" "$<" "$$ms"; \
			printf "%s\n" "$$out" >&2; \
		fi \
	) 200>$(LOCK_FILE); \
	exit $$rc
endef

define link_bin
	@t0=$$(date +%s%N); \
	printf "$(DIM)[link]$(RESET)   $(BOLD)%-40s$(RESET)" "$@"; \
	out=$$($(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS) 2>&1); rc=$$?; \
	t1=$$(date +%s%N); ms=$$(( (t1 - t0) / 1000000 )); \
	if [ $$rc -eq 0 ]; then \
		printf " $(CYAN)%dms$(RESET)\n" $$ms; \
	else \
		printf " $(ORANGE)FAILED$(RESET) $(CYAN)%dms$(RESET)\n" $$ms; \
		printf "%s\n" "$$out" >&2; \
	fi; \
	exit $$rc
endef

.PHONY : all clean install uninstall docs man_install man_uninstall debug-objs
.DEFAULT_GOAL := all

all : $(name)

install :
	install -d $(DESTDIR)$(prefix)/bin
	install $(name) $(DESTDIR)$(prefix)/bin/$(name)
	install scopen $(DESTDIR)$(prefix)/bin/scopen
	install -d $(DESTDIR)$(HELPDIR)
	install -m 644 doc $(DESTDIR)$(HELPDIR)/$(name)_help
	install -m 644 $(wildcard plot_*) $(DESTDIR)$(HELPDIR)/
	install -d $(DESTDIR)$(MANDIR)/
	install -m 644 xc.1 $(DESTDIR)$(MANDIR)/$(name).1
	install -d $(DESTDIR)$(THEMEDIR)
	install -m 644 ../themes/* $(DESTDIR)$(THEMEDIR)

uninstall :
	-rm $(DESTDIR)$(prefix)/bin/$(name)
	-rm $(DESTDIR)$(HELPDIR)/$(name)_help
	-rm $(DESTDIR)$(MANDIR)/$(name).1
	-rm -r $(DESTDIR)$(HELPDIR)

$(name) : $(OBJS)
	$(call link_bin)

$(name)qref: sc.h
	$(CC) $(CFLAGS) $(LDFLAGS) -DQREF $(QREF_FMT) -DSCNAME=\"$(name)\" -o $(name)qref help.c $(LDLIBS)

# Generated headers are still an order-only prerequisite for every object so
# the first build (or a grammar change) always regenerates them, while normal
# rebuilds rely on the -MMD/-MP .d files above for fine-grained incrementality.
$(OBJS) : | y.tab.h experres.h statres.h

%.o : %.c
	$(call compile_c)

.INTERMEDIATE: y.tab.c
y.tab.c	: gram.y
	$(YACC) -d $<

gram.c : y.tab.c
	mv $< $@

y.tab.h : gram.y | gram.c

experres.h : gram.y eres.sed
	sed -f eres.sed < $< > $@

statres.h : gram.y sres.sed
	sed -f sres.sed < $< > $@

pvmtbl.o: sc.h pvmtbl.c
	@t0=$$(date +%s%N); \
	out=$$($(CC) $(CFLAGS) -c -DPSC pvmtbl.c -o $@ 2>&1); rc=$$?; \
	t1=$$(date +%s%N); ms=$$(( (t1 - t0) / 1000000 )); \
	if [ $$rc -eq 0 ]; then \
		printf "$(DIM)[psc]$(RESET)    $(GREEN)%-8s$(RESET) $(BOLD)%-40s$(RESET) -> %-40s\t$(CYAN)%dms$(RESET)\n" "$(CC)" "pvmtbl.c" "$@" "$$ms"; \
	else \
		printf "$(DIM)[psc]$(RESET)    $(ORANGE)%-8s$(RESET) $(BOLD)%-40s$(RESET) FAILED\t$(CYAN)%dms$(RESET)\n" "$(CC)" "pvmtbl.c" "$$ms"; \
		printf "%s\n" "$$out" >&2; \
	fi; \
	exit $$rc

debug-objs:
	@echo "TOTAL_OBJS = $(TOTAL_OBJS)"
	@echo "$(OBJS)" | tr -s ' ' '\n' | sed '/^$$/d' | sort | uniq -c | sort -rn | head -20

docs:
	doxygen Doxyfile

man_install:
	@cp -r ../docs/man/man3/ /usr/local/share/man/
	mandb

# "xc" MUST match what is in Doxyfile `MAN_EXTENSION = .xc.3`
man_uninstall:
	@rm -rf /usr/local/share/man/man3/*xc.3
	@mandb

clean:
	rm -f $(OBJS) $(DEPS)
	rm -f experres.h statres.h y.tab.h
	rm -f core gram.c y.output pxmalloc.c pvmtbl.c tags $(name)qref
	rm -f qhelp.c $(name)
	rm -f $(COUNT_FILE) $(LOCK_FILE)
	rm -rf ../docs/

-include $(DEPS)
