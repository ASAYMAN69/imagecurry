#!/bin/bash

g++ -std=c++17 -o a_cpp main.cpp handlers.cpp http_response.cpp utils.cpp logging.cpp \
    -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L

if [ $? -eq 0 ]; then
    echo "Compilation successful! Executable created: a_cpp"
    ls -lh a_cpp
else
    echo "Compilation failed!"
    exit 1
fi
