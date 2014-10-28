#!/bin/bash
buildir=/tmp/cmake_build
logfile=/tmp/cmake_build.log
obfuscation_so_path=$PWD/build/llvm-passes/LLVMObfuscateZero.so

if [ ! -d $buildir ]
then
    mkdir $buildir
fi

if [ ! -d "cmake" ]
then
    git clone git://cmake.org/cmake.git || exit 2
fi

cd cmake || exit 2
git clean -fd
CC=clang CXX=clang++ cmake CMakeLists.txt -DCMAKE_CXX_FLAGS="-Wall -std=c++11 -fdiagnostics-color -g -Xclang -load -Xclang $obfuscation_so_path" -DCMAKE_C_FLAGS="-Wall -fdiagnostics-color -g -Xclang -load -Xclang $obfuscation_so_path" > /dev/null || exit 2
make -j > $logfile 2>&1
retcode=$?
echo "Compilation exited with retcode $retcode"
test $retcode -eq 0 || exit 3
make test >> $logfile 2>&1
retcode=$?
echo "Tests exited with retcode $retcode"
test $retcode -eq 0 || exit 4
