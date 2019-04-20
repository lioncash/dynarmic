#!/bin/sh

set -e
set -x

export CC=gcc-7
export CXX=g++-7
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH
export UNICORNDIR=$(pwd)/externals/unicorn

mkdir build && cd build
cmake .. -DBoost_INCLUDE_DIRS=${PWD}/../externals/ext-boost -DCMAKE_BUILD_TYPE=Release -DDYNARMIC_USE_LLVM=1 -DDYNARMIC_TESTS_USE_UNICORN=1 -DDYNARMIC_ENABLE_CPU_FEATURE_DETECTION=0 -G Ninja
ninja

./tests/dynarmic_tests --durations yes
