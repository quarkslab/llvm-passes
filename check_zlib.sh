#!/bin/bash
buildir=/tmp/zlib_build
logfile=/tmp/zlib_build.log
obfuscation_so_path=$PWD/build/llvm-passes/LLVMObfuscateZero.so

if [ ! -d $buildir ]
then
    mkdir $buildir
fi

if [ ! -d "zlib-1.2.8.tar.gz" ]
then
    wget http://zlib.net/zlib-1.2.8.tar.gz || (echo "Failed to download zlib." && exit 2)
fi

rm -rf zlib-1.2.8
tar xf zlib-1.2.8.tar.gz
cd zlib-1.2.8 || exit 2
prefix=/tmp/build CC=clang CFLAGS="-Xclang -load -Xclang $obfuscation_so_path" ./configure || exit 2
make > $logfile 2>&1
retcode=$?
echo "Compilation exited with retcode $retcode"
test $retcode -eq 0 || exit 3
make test >> $logfile 2>&1
retcode=$?
echo "Tests exited with retcode $retcode"
test $retcode -eq 0 || exit 4
rm -rf zlib-1.2.8
