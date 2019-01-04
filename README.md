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
* [Build & Install](#build--install)
* [Origin & References](#origin--references)
* [License](#license)


# Introduction

[brb_framework]() is a Framework of BrByte, written in C, containing the libraries and codes used by the company in the distribution of BrbOS (https://brbos.brbyte.com), a release based on FreeBSD.

For now we are publishing the [libbrb_core](BRBCORE.md). 

The main library is the core. That contains an abstraction for an event-oriented base, using kqueue (https://www.freebsd.org/cgi/man.cgi?kqueue).

Origin & References
-------------------

[libbrb_core][] is developed and maintained by [BrByte][https://brbyte.com]. It is built for and developed on FreeBSD system, patches to are most welcome.

## License

This project is licensed under the BSD 2-Clause License - see the [LICENSE.md](LICENSE.md) file for details

