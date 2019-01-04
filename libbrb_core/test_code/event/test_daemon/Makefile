#CC=gcc

INCLUDE= -I. -I ./include -I /include -I /usr/include -I /usr/include/bits -I /usr/include/kqueue -I /usr/local/include
LDFLAGS     = -g -O0

#XTRA_LIBS 	= -lm -lbrb_core -lbsd -lpthread -lkqueue -lrt -lssh2 -lssl -lcrypto
XTRA_LIBS 	= -lm -lbrb_core -lpthread -lssh2 -lssl -lcrypto

detected_OS := $(shell uname -s)
ifeq ($(detected_OS),Linux)
  XTRA_LIBS+= -lbsd -lkqueue -lrt
endif

OBJS		=  \
	test_daemon.o \

PROGS	= test_daemon
CFILES  = $(patsubst %.o,%.c,$(OBJS)) 
CFLAGS  = -g -O2 $(AC_CFLAGS) $(INCLUDE)
LIBS    = -L. -L /lib -L /usr/local/lib -L /usr/lib

.SUFFIXES: .o

.c.o:
	${CC} ${CFLAGS} ${DEFS} ${DEBUG} -Wno-comment -c -o $@ $<

all:	$(PROGS)

$(OBJS):	Makefile


test_daemon:	$(OBJS) Makefile $<
		$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS) $(XTRA_LIBS)

depend:
	makedepend $(CFLAGS) $(DEFS) $(CFILES)

.DONE:	query

query:
	-sbquery nosuchthing

clean:
	-rm -rf  $(OBJS) $(XOBJS) *pure_* core $(PROGS)
	
	