#!/bin/sh
valgrind --tool=memcheck --track-fds=yes --time-stamp=yes --log-file=./val_c.log --leak-check=full --show-reachable=no --track-origins=yes ./unix_client
valgrind --tool=memcheck --track-fds=yes --time-stamp=yes --log-file=./val_s.log --leak-check=full --show-reachable=no --track-origins=yes ./unix_server
