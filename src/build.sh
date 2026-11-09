#!/bin/sh
# Compila no Linux com MinGW-w64 (sudo apt install gcc-mingw-w64-x86-64)
set -e
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres -c 1252 resource.rc -O coff -o resource.o
x86_64-w64-mingw32-gcc -municode -mwindows -Os -s -static -Wall -Wno-unused-parameter \
  WatermarkRemover.c resource.o -o WatermarkRemoverByBrendler.exe \
  -lwinhttp -ldbghelp -lcomctl32 -lshell32 -ladvapi32 -luser32
echo "OK: WatermarkRemoverByBrendler.exe"
