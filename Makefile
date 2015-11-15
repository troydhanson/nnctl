PREFIX=/usr/local
OBJS=libnnctl.a nnctl
SUBDIRS= libut
all: $(SUBDIRS) $(OBJS)
CFLAGS= -I. -I./libut/include -I./tpl
#CFLAGS+=-O2
CFLAGS+=-g 

tpl.o: tpl/tpl.c
	$(CC) $(CFLAGS) -c $<

libnnctl.a: libnnctl.o $(SUBDIRS)/libut.a tpl.o
	ar cr $@ $^

nnctl.o: nnctl.c 
	$(CC) $(CFLAGS) -c $<

nnctl: nnctl.o tpl.o
	$(CC) $(CFLAGS) -o $@ $^ -lnanomsg -lreadline 

install: $(OBJS)
	cp nnctl $(PREFIX)/bin
	cp libnnctl.a $(PREFIX)/lib
	cp nnctl.h $(PREFIX)/include

.PHONY: clean $(SUBDIRS) sample

$(SUBDIRS):
	for f in $(SUBDIRS); do make -C $$f; done

sample: libnnctl.a nnctl
	make -C sample

clean:
	rm -f *.o $(OBJS)
	for f in $(SUBDIRS) sample; do make -C $$f clean; done
