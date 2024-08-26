# $FreeBSD$

LIB=brb_aiopq
#SHLIBDIR?=/usr/local/lib
#SHLIB_MAJOR=1
#INTERNALLIB=yes
WARNS?=2
LIBDIR=/usr/local/lib
INCLUDEDIR=/usr/local/include
WITHOUT_PROFILE=yes
CFLAGS += -g -w
CFLAGS += -I${.CURDIR} -I./include -I/usr/local/include

#DPADD= -lm
LDADD=	

SRCS+=	\
		libbrb_aiopq.c \
		\

INCS=	\
		libbrb_aiopq.h \
		\
	


#MAN=	libbrb_core.3

MLINKS+= 

.include <bsd.lib.mk>
