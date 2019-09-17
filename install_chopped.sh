#!/bin/bash

export REQUIRES_RTTI=1
ROOT_DIR=$(pwd)

mkdir ./local
#  Build LLVM:
#download llvm 3.4.2 from: http://releases.llvm.org/download.html
#   cfe-3.4.2.src.tar   llvm-3.4.2.src.tar
cd $ROOT_DIR/local
cp /playpen/nesfield/my_dependencies/cfe-3.4.2.src.tar  .
cp /playpen/nesfield/my_dependencies/llvm-3.4.2.src.tar .
tar -xf llvm-3.4.2.src.tar
tar -xf cfe-3.4.2.src.tar
mv cfe-3.4.2.src llvm-3.4.2.src/tools/clang
mkdir llvm-3.4.2.obj
cd llvm-3.4.2.obj/
cmake ../llvm-3.4.2.src/
make -j 12


#Build with configure for klee and uc-klee:
#https://github.com/klee/klee/issues/508
cd $ROOT_DIR/local
cp -r llvm-3.4.2.src configure_llvm-3.4.2.src
mkdir configure_llvm-3.4.2.obj
cd configure_llvm-3.4.2.obj
../configure_llvm-3.4.2.src/configure
make -j 12

#Build SVF (Pointer Analysis)
cd $ROOT_DIR/local
git clone https://github.com/davidtr1037/SVF.git
cd SVF
mkdir build
cd build
cmake \
    -DLLVM_DIR=$ROOT_DIR/local/llvm-3.4.2.obj/share/llvm/cmake/ \
    -DLLVM_SRC=$ROOT_DIR/local/llvm-3.4.2.src \
    -DLLVM_OBJ=$ROOT_DIR/local/llvm-3.4.2.obj \
    -DCMAKE_BUILD_TYPE:STRING=Release \
    ..
    make -j 12

#Build DG slicer:
cd $ROOT_DIR/local
git clone https://github.com/davidtr1037/dg.git
cd dg
mkdir build
cd build
cmake \
    -DLLVM_SRC_PATH=$ROOT_DIR/local/llvm-3.4.2.src \
    -DLLVM_BUILD_PATH=$ROOT_DIR/local/llvm-3.4.2.obj \
    -DLLVM_DIR=$ROOT_DIR/local/llvm-3.4.2.obj/share/llvm/cmake/ \
    ..
make -j 12

#Build UC-Klee
cd $ROOT_DIR/local
git clone git@git.cs.unc.edu:cliver/klee-uclibc.git
cd klee-uclibc
git checkout chopped_dependency
./configure --make-llvm-lib \
    --with-llvm=$ROOT_DIR/local/configure_llvm-3.4.2.obj/Release+Asserts/bin/llvm-config
make -j 12
cd ..
mkdir install
make PREFIX=$ROOT_DIR/local/klee-uclibc/install install

#Build KLEE:
cd $ROOT_DIR
git clone git@git.cs.unc.edu:cliver/klee chopper
cd chopper
git checkout chop_cliver
mkdir klee_build
cd klee_build
CXXFLAGS="-fno-rtti" cmake \
    -DENABLE_SOLVER_STP=ON \
    -DENABLE_POSIX_RUNTIME=ON  \
    -DENABLE_KLEE_UCLIBC=ON \
    -DKLEE_UCLIBC_PATH=$ROOT_DIR/local/klee-uclibc \
    -DLLVM_CONFIG_BINARY=$ROOT_DIR/local/configure_llvm-3.4.2.obj/Release+Asserts/bin/llvm-config \
    -DENABLE_UNIT_TESTS=OFF \
    -DKLEE_RUNTIME_BUILD_TYPE=Release+Asserts \
    -DENABLE_SYSTEM_TESTS=ON \
    -DSVF_ROOT_DIR=$ROOT_DIR/local/SVF \
    -DDG_ROOT_DIR=$ROOT_DIR/local/dg \
    ..
make -j 12


