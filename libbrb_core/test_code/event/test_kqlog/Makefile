#CC=cc

LDFLAGS+= -g -O2
#DEBUG_FLAGS+= -Wno-comment

PROG=test_kqlog
SRCS=test_kqlog.c \
	
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