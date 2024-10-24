#! /usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

OPENCV_LIB="{SCRIPT_DIR}/lib/opencv/lib"
ZMQ_LIB="{SCRIPT_DIR}/lib/zeromq/lib"
ZMQCPP_LIB="{SCRIPT_DIR}/lib/cppzmq/lib"

unameOut="$(uname -s)"
case "${unameOut}" in
    Linux*)     MILLICAST_LIB="{SCRIPT_DIR}/lib/millicast/lib/x86_64-linux-gnu"; \
                MILLICAST_EXT_LIB="{SCRIPT_DIR}/lib/millicast/libexec/millicastsdk";;
    Darwin*)    MILLICAST_LIB="{SCRIPT_DIR}/lib/millicast/lib";;
    *)          echo "Un-support system: ${unameOut}"; exit 1;;
esac


export LD_LIBRARY_PATH=${OPENCV_LIB}:${ZMQ_LIB}:${ZMQCPP_LIB}:${MILLICAST_LIB}:${MILLICAST_EXT_LIB}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}
