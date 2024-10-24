#! /usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="${SCRIPT_DIR}/.."

THIRD_DIR="${ROOT_DIR}/3rd"
APP_LIB_DIR="${ROOT_DIR}/lib"
INSTALL_DIR="${APP_LIB_DIR}/zeromq"

echo
mkdir -p ${INSTALL_DIR}


mkdir -p ${THIRD_DIR}/zmq_build
cd ${THIRD_DIR}/zmq_build

cmake -D WITH_PERF_TOOL=OFF \
    -D ZMQ_BUILD_TESTS=OFF \
    -D ENABLE_CPACK=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    ../zeromq-4.3.5

make -j10
make install

cd ..
rm -rf ${THIRD_DIR}/zmq_build
