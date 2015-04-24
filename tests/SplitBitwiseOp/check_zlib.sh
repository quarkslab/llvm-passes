# RUN: sh %s
set -e
builddir=zlib_build

rm -rf $builddir
mkdir $builddir
cd $builddir

wget http://zlib.net/zlib-1.2.8.tar.gz
rm -rf zlib-1.2.8
tar xf zlib-1.2.8.tar.gz
cd zlib-1.2.8
prefix=$PWD CC=clang CFLAGS="-Xclang -load -Xclang LLVMSplitBitwiseOp.so" ./configure
make
make test

cd ..
rm -rf $builddir
