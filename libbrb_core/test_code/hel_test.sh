#!/bin/sh
valgrind --tool=helgrind --read-var-info=yes --time-stamp=yes --log-file=./helgrind.log ./test_unit
