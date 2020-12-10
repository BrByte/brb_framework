# libbrb_core | kqueue event loop for FreeBSD
====================================

Introduction
------------

The main library of BrByte Framework ([brb_framework](README.md)) is the libbrb_core.

A kqueue event-based library with functions to provide a useful set of functionalities. 

## lib_brbcore/comm

Sources to do icmp, ipc, serial, tcp, udp, unix, resolver, stats, ftp, snmp, ssh, ssl, tftp, tzsp.

## lib_brbcore/crypto

Sources to do base64, blowfish, md5, rc4, sha1.

## lib_brbcore/data

Sources to do double linked list, dynamic array, dynamic bitmap, hash tables, linked lists, memory arena, memory buffer, memory_strem, radix_tree, slotqueue.
assoc_array, ipv4_table, key_value, mem_buf_mapped, mem_slot, meta_data, queue, speed_regex, string_array, string_assoc_array, string.

## lib_brbcore/event

That contains an abstraction for an event loop, using kqueue (https://www.freebsd.org/cgi/man.cgi?kqueue).



Getting Started
---------------

These instructions will get you a copy of the library up and running on your local machine for development and testing purposes. 


### Prerequisites

libbrb_core depends on libssh2, to install in FreeBSD, use ports

```sh
$ cd /usr/ports/security/libssh2
/usr/ports/security/libssh2$ make install clean
```

On linux, update and install packages

```sh
joe /etc/apt/sources.list
deb http://ftp.de.debian.org/debian jessie main

wget http://ftp.us.debian.org/debian/pool/main/libk/libkqueue/libkqueue-dev_2.0.3-1.1_armhf.deb
wget http://ftp.us.debian.org/debian/pool/main/libk/libkqueue/libkqueue0_2.0.3-1.1_armhf.deb 
apt-install libkqueue0_2.0.3-1.1_armhf.deb
apt-install libkqueue-dev_2.0.3-1.1_armhf.deb
```

```sh
apt-get update
apt-get install joe
apt-get install libbsd-dev
apt-get install zlib1g-dev // libz-dev
apt-get install libevent-dev
apt-get install libkqueue-dev
apt-get install libssh2-1-dev
apt-get install libssl1.0-dev

```

### Build & Install

To compile lib_brbcore, download or clone the source, and go to *lib_brbcore* directory 

```sh
$ cd ./brb_framework/lib_brbcore
./brb_framework/lib_brbcore$ make
./brb_framework/lib_brbcore$ make install
```
On linux, before make
```sh
$
./brb_framework/lib_brbcore$ rm Makefile
./brb_framework/lib_brbcore$ cp Makefile.linux Makefile
./brb_framework/lib_brbcore$ make  
./brb_framework/lib_brbcore$ make install
```


### Test Codes

To compile a test code, go to the desired directory and do a make

```sh
$ cd ./brb_framework/libbrb_core/test_code/data/test_mem_buf

./brb_framework/libbrb_core/test_code/data/test_mem_buf$ make

./brb_framework/libbrb_core/test_code/data/test_mem_buf$ ./test_mem_buf
```

## Using -lbrbcore

libbrb_core is by default installed as a library with a few header files, you only need to include one:

```C
#include <libbrb_core.h>
```

and in your `Makefile`:

```sh
LDADD= -lm -lssh2 -lssl -lbrb_core
```


Core Events
-----------

### Create an Event Context

To monitor events the developer first creates an *event base*, this
is achieved by calling `EvKQBaseNew()` with a pointer to a (thread) local
`EvKQBase` variable.

```C
#include <libbrb_core.h>
EvKQBase *glob_ev_base;

/* Create event base */
glob_ev_base	= EvKQBaseNew(NULL);

```

### Register an Event

For each event to monitor, be it a signal, cron/timer or a file/network descriptor, it must be registered with the event base.
The `EvBaseKQFileDesc`, is registered by calling `EvKQBaseFDGenericInit()`
function with the `EvKQBase` context, the callback, and an optional void callback data.


### Start Event Loop

When all events are registered, call the *event loop* with `EvKQBaseDispatch()` and the argument to the event base, `timeout_ms` to the time off events.

```C
	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);
```

Here is a example to get file modification:

```C

#include <libbrb_core.h>

EvKQBase *glob_ev_base;

static EvBaseKQCBH filemon_cb;

static int filemon_cb(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
    printf("filemon_cb - received action [%d] on fd [%d] - thrd_id [%d]\n", action, fd, thrd_id);

    return 1;
}

int main(int argc, char **argv)
{
	int file_fd;

	glob_ev_base	= EvKQBaseNew(NULL);

	file_fd 			= open("./file.txt", (O_WRONLY | O_TRUNC | O_CREAT ));

	/* Initialize internal KQ_FD */
	EvKQBaseFDGenericInit(glob_ev_base, file_fd, FD_TYPE_FILE);

	EvKQBaseSetEvent(glob_ev_base, file_fd, COMM_EV_FILEMON, COMM_ACTION_ADD_PERSIST, filemon_cb, NULL);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	return 1;
}
```

### Summary

1. Set up an event base with `EvKQBaseNew()`
2. Register FD with the event base using `EvKQBaseFDGenericInit()`
3. Register event callbacks with the event context using `EvKQBaseSetEvent()`
4. Start the event loop with `EvKQBaseDispatch()`

**Note 1:** Make sure to use non-blocking stream I/O!  Most hard to find bugs in event driven applications are due to sockets and files being opened in blocking mode. Be careful out there!
  Use `EvKQBaseSocketSetNonBlock()` to avoid blocking I/O.

```C
	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(kq_base, socket_fd);
```

**Note 2:** When closing a descriptor or socket, make sure to first stop your handler, if possible. This will help prevent any nasty side effects on your program.

```C
	/* Close the socket and cancel any pending events */
	EvKQBaseSocketClose(kq_base, socket_fd);
```

