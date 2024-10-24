#! /usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="${SCRIPT_DIR}/.."

echo "ROOT_DIR: ${ROOT_DIR}"

OPENCV_LIB="{ROOT_DIR}/lib/opencv/lib"
ZMQ_LIB="{ROOT_DIR}/lib/zeromq/lib"
ZMQCPP_LIB="{ROOT_DIR}/lib/cppzmq/lib"

unameOut="$(uname -s)"
case "${unameOut}" in
    Linux*)     MILLICAST_LIB="{ROOT_DIR}/lib/millicast/lib/x86_64-linux-gnu"; \
                MILLICAST_EXT_LIB="{ROOT_DIR}/lib/millicast/libexec/millicastsdk";;
    Darwin*)    MILLICAST_LIB="{ROOT_DIR}/lib/millicast/lib";;
    *)          echo "Un-support system: ${unameOut}"; exit 1;;
esac


export LD_LIBRARY_PATH=${OPENCV_LIB}:${ZMQ_LIB}:${ZMQCPP_LIB}:${MILLICAST_LIB}:${MILLICAST_EXT_LIB}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}
