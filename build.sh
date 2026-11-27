#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -pedantic -Iinclude src/main.cpp -o reconstruct
printf '%s\n' "Built: $(pwd)/reconstruct"
