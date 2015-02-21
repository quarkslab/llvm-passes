# RUN: sh %s
set -e
builddir=openssl_build

rm -rf $builddir
mkdir $builddir
cd $builddir

git clone --depth 1 --single-branch git://git.openssl.org/openssl.git
cd openssl
CC=clang ./Configure --openssldir=$PWD/build linux-x86_64 "-Xclang -load -Xclang LLVMObfuscateZero.so" > /dev/null || exit 2
make -j2
make test

cd ..
rm -rf $builddir
