# brb_framework


# Introduction


# BrByte Framework | brb_framework
====================================

* [Introduction](#introduction)
* [libbrb_core](BRBCORE.md)
  * [Create an Event Context](BRBCORE.md#create-an-event-context)
  * [Register an Event](BRBCORE.md#register-an-event)
  * [Start Event Loop](BRBCORE.md#start-event-loop)
  * [Summary](BRBCORE.md#summary)
* [Origin & References](#origin--references)
* [Work](#work)


# Introduction

[brb_framework]() is a Framework of BrByte, written in C, containing the libraries and codes used by the company in the distribution of BrbOS (https://brbos.brbyte.com), a release based on FreeBSD.

For now we are publishing the [libbrb_core](BRBCORE.md). 

The main library is the [libbrb_core](BRBCORE.md). That contains an abstraction for an event-oriented base, using kqueue (https://www.freebsd.org/cgi/man.cgi?kqueue).

Origin & References
-------------------

[libbrb_core](BRBCORE.md) is developed and maintained by BrByte (https://software.brbyte.com).

It is built for and developed on FreeBSD system, patches to are most welcome.

On Linux you can use libkqueue.  

## License

This project is licensed under the BSD 2-Clause License - see the [LICENSE.md](LICENSE.md) file for details

## Work

There is more to come.
