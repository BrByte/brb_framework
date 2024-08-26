#PROGS		= test_daemon
#INCLUDE     = -I. -I./include -I/usr/local/include -I/usr/include
#LDFLAGS     = 
#CFLAGS  	= -g -O2 $(AC_CFLAGS) $(INCLUDE)
#
#XTRA_LIBS 	= -lz -lc -lm -lpthread
##XTRA_LIBS 	+= -lssl -lssh2 -lcrypto
#XTRA_LIBS 	+= -lbrb_core
#
##ifeq ($(HAS_PROFILE), 1)
##XTRA_LIBS	+= -lprofiler
##endif
#
##UNAME_S := $(shell uname -s)
##ifeq ($(UNAME_S),Linux)
##INCLUDE 	+= -I /usr/include/kqueue
##XTRA_LIBS 	+=  -L /usr/lib/x86_64-linux-gnu -lbsd -lkqueue -lrt
##endif
#
#LIBS    	= -L. -L /lib -L /usr/lib -L /brb_main/usr/local/lib -L /usr/local/lib $(XTRA_LIBS)
#
#SRC	=  \
#	test_daemon.c \
#	\
#
#OBJS 	= $(SRC:.c=.o)
#CFILES  = $(patsubst %.o, %.c, $(OBJS)) 
#
#.SUFFIXES: .o
#
#.c.o:	
#	${CC} ${CFLAGS} ${DEFS} ${DEBUG} -Wno-comment -fcommon -c -o $@ $<
#
#all:	$(PROGS)
#
#$(OBJS):	Makefile
#
#$(PROGS):	$(OBJS) Makefile $<
#	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)
#
#depend:
#	makedepend $(CFLAGS) $(DEFS) $(CFILES)
#
#.DONE:	query
#
#query:
#	-sbquery nosuchthing
#
#clean:
#	-rm -rf  $(OBJS) $(XOBJS) *pure_* *core $(PROGS)

#CC=cc

LDFLAGS+= -g -O2
#DEBUG_FLAGS+= -Wno-comment

PROG=test_daemon
SRCS=test_daemon.c \
	
#OBJS+=  ${SRCS:R:S/$/.o/g}

WARNS?=	0
MAN=
CFLAGS+= -L. -L /usr/local/lib -I. -I./include -I/usr/local/include -I./includes
LDADD= -lm -lz -lpthread -lssh2 -lssl -lcrypto -lbrb_core
.SUFFIXES: .o

.c.o:	
	${CC} ${CFLAGS} ${DEFS} ${DEBUG} -Wno-comment -c -o $@ $<

.if !target(clean)
clean:
	rm -f a.out [Ee]rrs mklog ${PROG}.core ${PROG} ${OBJS} ${CLEANFILES}
.endif

.include <bsd.subdir.mk>
.include <bsd.prog.mk>