SRCS = $(wildcard server*.c) 
PROGS = $(patsubst %.c,%,$(SRCS))

LIBDIR = ..
LIB = $(LIBDIR)/libnnctl.a
LDFLAGS = -L$(LIBDIR) -lnnctl -lnanomsg

CFLAGS = -I$(LIBDIR)
CFLAGS += -g
CFLAGS += -Wall 
CFLAGS += ${EXTRA_CFLAGS}

all: $(PROGS)

$(LIB): 
	@echo 'run make in parent directory then try again'; false

$(PROGS): $(SRCS) $(LIB) 
	$(CC) $(CFLAGS) -o $@ $(@).c $(LDFLAGS)

.PHONY: clean

clean:	
	rm -f $(PROGS) 
