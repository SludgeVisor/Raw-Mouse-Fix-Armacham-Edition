#!/usr/bin/env bash
# Cross-compiles msacm32.dll (32-bit) on Linux/macOS/WSL with LLVM. No Windows SDK or CRT needed.
# Requires: clang, lld-link, llvm-dlltool
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build && cd build
llvm-dlltool -m i386 -d ../src/kernel32.def -l kernel32.lib
llvm-dlltool -m i386 -d ../src/user32.def   -l user32.lib
clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -nostdinc \
      -fno-stack-protector -mno-sse -fno-asynchronous-unwind-tables -Wall -Wextra \
      -c ../src/rawmouse.c -o rawmouse.obj
lld-link /dll /nodefaultlib /entry:DllMain@12 /subsystem:windows /machine:x86 \
      /def:../src/exports.def /out:msacm32.dll /safeseh:no /nxcompat \
      rawmouse.obj kernel32.lib user32.lib
echo "Built build/msacm32.dll"
