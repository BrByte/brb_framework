# $FreeBSD$

LIB=brb_json
#SHLIBDIR?=/usr/local/lib
#SHLIB_MAJOR=1
#INTERNALLIB=yes
WARNS?=1
LIBDIR=/usr/local/lib
INCLUDEDIR=/usr/local/include
WITHOUT_PROFILE=yes
CFLAGS += -g -w
CFLAGS += -I${.CURDIR} -I./include -I/usr/local/include
LDADD =	

SRCS +=	\
		libbrb_json.c \
		api/api_array.c \
		api/api_error.c \
		api/api_object.c \
		api/api_parser.c \
		api/api_value.c \
		json/json_array.c \
		json/json_object.c \
		json/json_value.c \
		\

INCS =	\
		libbrb_json.h \
		\

#MAN=	libbrb_json.3

MLINKS += 

.include <bsd.lib.mk>
