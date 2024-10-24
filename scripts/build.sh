#! /usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="${SCRIPT_DIR}/.."

THIRD_DIR="${ROOT_DIR}/3rd"
APP_LIB_DIR="${ROOT_DIR}/lib"

mkdir -p ${ROOT_DIR}/build

cd ${ROOT_DIR}/build


source ${SCRIPT_DIR}/source_lib.sh
cmake  \
    -DCMAKE_BUILD_TYPE=Release \
    ..

make -j8
