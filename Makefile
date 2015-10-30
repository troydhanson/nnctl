OBJS=nnctl
all: $(OBJS)
CFLAGS= -I.
CFLAGS+=-g 

nnctl: nnctl.c 
	$(CC) $(CFLAGS) -c $<
	$(CC) $(CFLAGS) -o $@ $< -lnanomsg -lreadline 

.PHONY: clean

clean:
	rm -f *.o $(OBJS)
