#!/bin/bash
buildir=/tmp/openssl_build
logfile=/tmp/openssl_build.log
obfuscation_so_path=$PWD/build/llvm-passes/LLVMObfuscateZero.so

if [ ! -d $buildir ]
then
    mkdir $buildir
fi

if [ ! -d "openssl" ]
then
    git clone git://git.openssl.org/openssl.git || exit 2
fi

cd openssl || exit 2
git clean -fd
CC=clang ./Configure --openssldir=$buildir linux-x86_64 "-Xclang -load -Xclang $obfuscation_so_path" > /dev/null || exit 2
make -j > $logfile 2>&1
retcode=$?
echo "Compilation exited with retcode $retcode"
test $retcode -eq 0 || exit 3
make test >> $logfile 2>&1
retcode=$?
echo "Tests exited with retcode $retcode"
test $retcode -eq 0 || exit 4
