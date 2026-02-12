#!/bin/bash
set -e

mkdir -p build
cmake -S . -B build

TARGET="$1"

if [ -z "$TARGET" ]; then
  cmake --build build -j
else
  cmake --build build -j --target examples_all --target "$TARGET"
fi
