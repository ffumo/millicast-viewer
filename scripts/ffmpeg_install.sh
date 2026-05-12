#! /usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_DIR="${SCRIPT_DIR}/.."

THIRD_DIR="${ROOT_DIR}/3rd"
APP_LIB_DIR="${ROOT_DIR}/lib"
INSTALL_DIR="${ROOT_DIR}/lib/ffmpeg"

echo
mkdir -p ${INSTALL_DIR}

cd ${THIRD_DIR}/ffmpeg

unameOut="$(uname -s)"
case "${unameOut}" in
  Linux*)     INDEV_FLAG=v4l2;;
  Darwin*)    INDEV_FLAG=avfoundation;;
  # For windows use INDEV_FLAG=dshow
  *)          echo "Un-support system: ${unameOut}"; exit 1;;

esac
./configure \
  --prefix="${INSTALL_DIR}" \
  --enable-version3 \
  --enable-shared \
  --disable-static \
  --enable-gpl \
  --enable-swscale \
  --enable-avformat \
  --enable-avcodec \
  --enable-libx264 \
  --enable-openssl \
  --enable-avdevice \
  --enable-indev=${INDEV_FLAG} \
  --enable-protocol=rtp,rtsp,udp,tcp,http,rtmp,rtmps,https \
  --cc="gcc -fPIC"

#   enable-indev: v4l2/avfoundation/dshow


make -j8
make install


