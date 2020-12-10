CC=cc

INCLUDE     = -I. -I./include -I/usr/local/include -I/usr/include/kqueue/
LDFLAGS     = -g -O0
XTRA_LIBS 	= -lpthread -lz -lc -lm -lssl -lcrypto -lssh2 \
 				-lbrb_core
#  -lkqueue -lbsd

OBJS	=  \
	test_comm_client.o \
	
CFILES  = $(patsubst %.o,%.c,$(OBJS)) 

PROG	= test_comm_client
CFLAGS  = -g -O2 $(AC_CFLAGS) $(INCLUDE)
LIBS    = -L. $(XTRA_LIBS) -L /usr/local/lib

.SUFFIXES: .o

.c.o:	
	${CC} ${CFLAGS} ${DEFS} ${DEBUG} -Wno-comment -c -o $@ $<
#	${CC} ${CFLAGS} ${DEFS} ${DEBUG} -g -c $<

all:	$(PROG)

$(OBJS):	Makefile

$(PROG):	$(OBJS) Makefile $<
	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)
#	$(CC) -o $@ $(LDFLAGS) $(OBJS)  -Wl,-export-dynamic $(LIBS)

depend:
	makedepend $(CFLAGS) $(DEFS) $(CFILES)

.DONE:	query

query:
	-sbquery nosuchthing

clean:
	-rm -rf  $(OBJS) $(XOBJS) *pure_* *.core $(PROG)
