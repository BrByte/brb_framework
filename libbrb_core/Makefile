# $FreeBSD$

# The general ``build'' order is: 
# PROG/[SH]LIB/SCRIPTS SRCS WARNS CFLAGS	DPADD LDADD INCS FILES LINKS [NO_]MAN MLINKS.

LIB=brb_core
#SHLIBDIR?=/usr/local/lib
#SHLIB_MAJOR=2
#INTERNALLIB=yes
WARNS?=1
LIBDIR=/usr/local/lib
INCLUDEDIR=/usr/local/include
WITHOUT_PROFILE=yes
CFLAGS += -g -w
#CFLAGS += -w
CFLAGS += -I${.CURDIR} -I./include -I/usr/local/include

#DPADD= -lm
LDADD=	

SRCS+=	\
		comm/core/serial/comm_serial.c \
		comm/core/ipc/ev_ipc_base.c \
		comm/core/ipc/ev_ipc_child.c \
		comm/core/icmp/comm_icmp_base.c \
		comm/core/tcp/comm_tcp_aio.c \
		comm/core/tcp/comm_tcp_client_pool.c \
		comm/core/tcp/comm_tcp_client.c \
		comm/core/tcp/comm_tcp_server.c \
		comm/core/tcp/comm_tcp_server_conn.c \
		comm/core/unix/comm_unix_client_pool.c \
		comm/core/unix/comm_unix_client.c \
		comm/core/unix/comm_unix_server.c \
		comm/core/unix/comm_unix_aio.c \
		comm/core/comm_dns_resolver.c \
		comm/core/comm_statistics.c \
		comm/core/comm_desc_token.c \
		comm/utils/comm_ftp_client.c \
		comm/utils/comm_tftp_server.c \
		comm/utils/comm_tzsp_server.c \
		comm/utils/comm_ssh_client.c \
		comm/utils/comm_ssl_utils.c \
		comm/utils/comm_icmp_pinger.c \
		crypto/base64.c \
		crypto/blowfish.c \
		crypto/md5.c \
		crypto/rc4.c \
		crypto/sha1.c \
		data/core/dlinked_list.c \
		data/core/dyn_array.c \
		data/core/dyn_bitmap.c \
		data/core/hash_table.c \
		data/core/hash_table_v2.c \
		data/core/linked_list.c \
		data/core/mem_buf.c \
		data/core/mem_stream.c \
		data/core/mem_arena.c \
		data/core/radix_tree.c \
		data/core/slotqueue.c \
		data/utils/string_array.c \
		data/utils/string.c \
		data/utils/ipv4_table.c \
		data/utils/utils.c \
		data/utils/utils_nw.c \
		data/utils/speed_regex.c \
		data/utils/regex.c \
		data/utils/assoc_array.c \
		data/utils/string_assoc_array.c \
		data/utils/key_value.c \
		data/utils/queue.c \
		data/utils/meta_data.c \
		data/utils/mem_slot.c \
		data/utils/mem_buf_mapped.c \
		event/aio/ev_kq_aio_file.c \
		event/aio/ev_kq_aio_req.c \
		event/aio/ev_kq_aio_transform.c \
		event/core/ev_kq_base.c \
		event/core/ev_kq_defer.c \
		event/core/ev_kq_fd.c \
		event/core/ev_kq_ievents.c \
		event/core/ev_kq_jobs.c \
		event/core/ev_kq_object.c \
		event/core/ev_kq_signal.c \
		event/core/ev_kq_timer.c \
		event/core/ev_kq_timeout.c \
		event/utils/ev_kq_daemon.c \
		event/utils/ev_kq_logger.c \
		event/utils/ev_kq_file_mapped.c \
		thread/thrd_aio_base.c \
		thread/thrd_pool_base.c \
		\
		
INCS=	\
		include/libbrb_comm_proto.h \
		include/libbrb_comm_unix.h \
		include/libbrb_comm_utils.h \
		include/libbrb_core.h \
		include/libbrb_data.h \
		include/libbrb_ev_aio.h \
		include/libbrb_ev_kq.h \
		include/libbrb_ev_comm.h \
		include/libbrb_ev_core.h \
		include/libbrb_ev_utils.h \
		include/libbrb_ev_logger.h \
		include/libbrb_ev_ipc.h \
		include/libbrb_thread.h \



#MAN=	libbrb_core.3

MLINKS+= 

.include <bsd.lib.mk>
