#!/bin/bash

# get current directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Check if clang-format is installed
find ./ \( -name "*.cpp" -o -name "*.h" \) -exec clang-format -i {} +
