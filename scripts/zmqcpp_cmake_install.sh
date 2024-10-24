#! /usr/bin/env bash

# Add zeromq lib path to CMakeLists.txt

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="${SCRIPT_DIR}/.."

THIRD_DIR="${ROOT_DIR}/3rd"
APP_LIB_DIR="${ROOT_DIR}/lib"
INSTALL_DIR="${APP_LIB_DIR}/cppzmq"

mkdir -p ${INSTALL_DIR}

mkdir -p ${THIRD_DIR}/cppzmq_build
cd ${THIRD_DIR}/cppzmq_build

echo "${APP_LIB_DIR}"

cmake -DENABLE_DRAFTS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    -DZeroMQ_DIR=${APP_LIB_DIR}/zeromq \
    -D ZEROMQ_LIB_DIR=${APP_LIB_DIR}/zeromq/lib \
    -D ZEROMQ_INCLUDE_DIR=${APP_LIB_DIR}/zeromq/include \
    ../cppzmq-4.10.0

make -j10
make install

cd ..
rm -rf ${THIRD_DIR}/cppzmq_build