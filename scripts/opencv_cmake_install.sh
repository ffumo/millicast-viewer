#! /usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="${SCRIPT_DIR}/.."

echo "ROOT_DIR: ${ROOT_DIR}"

THIRD_DIR="${ROOT_DIR}/3rd"
APP_LIB_DIR="${ROOT_DIR}/lib"
INSTALL_DIR="${APP_LIB_DIR}/opencv"

mkdir -p ${INSTALL_DIR}


mkdir ${THIRD_DIR}/opencv_build
cd ${THIRD_DIR}/opencv_build

cmake -DOPENCV_EXTRA_MODULES_PATH=../opencv_contrib-4.10.0/modules \
    -DOPENCV_GENERATE_PKGCONFIG=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    ../opencv-4.10.0

make -j20
make install

cd ../..
rm -rf ${THIRD_DIR}/opencv_build