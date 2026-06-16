# Millicast viewer

Utility for capturing Millicast stream

## Setup

### Required build tools

- Gcc
- Cmake


### Download source code and third party libraries

- Clone project source code
    ```
    git clone https://github.com/ffumo/millicast-viewer.git
    ```

- Install Ubuntu package:

```
sudo apt install build-essential yasm nasm \
    libssl-dev libx264-dev libx265-dev \
    libvpx-dev libfdk-aac-dev libopus-dev
```

```
sudo apt install -y libx11-dev libxfixes-dev libxdamage-dev libxcomposite-dev libxtst-dev \
  libxrandr-dev libavahi-client3 libavahi-common3
```

```
sudo apt install -y unzip
```

- Install PulseAudio that required for Millicast SDK
```
sudo apt install pulseaudio 
```

- Enable linger for deployment user
```
sudo loginctl enable-linger msservice   
```



- Download thirdparty libraries<br/>
    
    Please consider your system architecture (Darwin or Linux)
    ```
    echo "$(uname)"
    ```

    - [Millicast SDK](https://github.com/millicast/millicast-native-sdk/releases) for stream subscription [MacOs Arm64]

        - Remove old MillicastSDK lib
        ```shell
        rm -rf 3rd/millicast.zip lib/millicast
        ```

        ```shell
        wget https://github.com/millicast/millicast-native-sdk/releases/download/v2.5.5/millicast-native-sdk-2.5.5-macos-arm64.zip -O 3rd/millicast.zip

        unzip 3rd/millicast.zip -d 3rd && mv 3rd/usr lib/millicast
        ```

    - [Millicast SDK](https://github.com/millicast/millicast-native-sdk/releases) for stream subscription [Ubuntu 22.04 x86] (Ignore this step if you installed millicast via **apt**)
        
        - Remove old MillicastSDK lib
        ```shell
        rm -rf 3rd/millicast.deb 3rd/debian-binary 3rd/control.tar.gz 3rd/data.tar.gz lib/millicast
        ```
        - Download latest version of SDK
        ```
        wget https://github.com/millicast/millicast-native-sdk/releases/download/v2.5.5/millicast-native-sdk-2.5.5-ubuntu22-x64-gnu-std.deb -O 3rd/millicast.deb

        ar vx 3rd/millicast.deb --output 3rd/

        tar -xvf 3rd/data.tar.gz --directory 3rd/ && mv 3rd/usr lib/millicast
        ```

        - Create static link to dependency
        ```shell
        ln -rs lib/millicast/libexec/millicastsdk/libndi.so.5.1.1 lib/millicast/libexec/millicastsdk/libndi.so.5
        ```

    - [FFmpeg] (https://ffmpeg.org/) for RTMP stream subscription 
        - Remove old FFMPEG lib
        ```shell
        rm -rf 3rd/ffmpeg lib/ffmpeg
        ```
        - Download latest version of SDK
        ```
        wget https://ffmpeg.org/releases/ffmpeg-8.1.1.tar.xz -O 3rd/ffmpeg-8.1.1.tar.xz
        ```
        ```
        tar -xvf 3rd/ffmpeg-8.1.1.tar.xz --directory 3rd/ && mv 3rd/ffmpeg-8.1.1 3rd/ffmpeg
        ```
        - Build FFmpeg
        ```
        ./scripts/ffmpeg_install.sh
        ```

    - [OpenCV](https://docs.opencv.org/4.x/d7/d9f/tutorial_linux_install.html) for image processing
        ```shell
        wget -O 3rd/opencv.zip https://github.com/opencv/opencv/archive/4.10.0.zip

        wget -O 3rd/opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/4.10.0.zip

        unzip 3rd/opencv.zip -d 3rd

        unzip 3rd/opencv_contrib.zip -d 3rd

        ./scripts/opencv_cmake_install.sh
        ```

    - [ZeroMQ](https://github.com/zeromq/libzmq/releases/tag/v4.3.5) for re-publishing image to python
        ```shell
        wget -O 3rd/zeromq.zip https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.zip

        unzip 3rd/zeromq.zip -d 3rd

        ./scripts/zeromq_cmake_install.sh
        ```

    - [CppZMQ](https://github.com/zeromq/cppzmq/releases/tag/v4.10.0) C++ wrapping of ZeroMQ
        ```shell
        wget -O 3rd/cppzmq.zip https://github.com/zeromq/cppzmq/archive/refs/tags/v4.10.0.zip
        
        unzip 3rd/cppzmq.zip -d 3rd

        ./scripts/zmqcpp_cmake_install.sh
        ```

    - [Json](https://github.com/nlohmann/json/releases/tag/v3.11.3) for parsing JSON configuration file
        ```shell
        wget -O 3rd/json.zip https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip

        unzip 3rd/json.zip "include/*" -d 3rd/ && mv 3rd/include 3rd/json
        ```

    - [Argparse](https://github.com/p-ranav/argparse/releases/tag/v3.1) for parsing commandline arguments
        ```shell
        wget -O 3rd/argparse.zip https://github.com/p-ranav/argparse/archive/refs/tags/v3.1.zip
        unzip -j 3rd/argparse.zip argparse-3.1/include/argparse/argparse.hpp -d 3rd/argparse/
        ```

### Build applications

Run scripts in [scripts](scripts) directory for build libs (except final build script [build.sh](script/build.sh))

```sh
source scripts/source_lib.sh

./scripts/build.sh
```

Note: Linking error under Linux, if you meet linking error "warning: libndi.so.5 ...", then create symbol link for missing symbol
```sh
ln -rs lib/millicast/libexec/millicastsdk/libndi.so.5.1.1 lib/millicast/libexec/millicastsdk/libndi.so.5
```

### Run Millicast Viewer app

After build success, app is located at [millicast_viewer](build/millicast_viewer).<br/>
To run app, you have to source dependent libraries [source_lib.sh](script/source_lib.sh)
```shell
source scripts/source_lib.sh

./build/millicast_viewer --help
```

App available options:
-  -h, --help     shows help message and exits 
-  -v, --version  prints version information and exits 
-  -c, --config   configuration file [required]
-  --id           stream id in configuration file [nargs=0..1] [default: 1]
-  -d, --display  enable display (on PC)
-  -t, --token    Stream token, can also set by STREAM_TOKEN env [nargs=0..1] [default: ""]
-  -q, --quiet    disable showing SDK stats 
-  -s, --sample   Run viewer in sampling mode 
-  --debug        enable debug logs 


Configuration file contains stream information. Please refer to [sample_config.json](config/sample_config.json)


### Run FFmpeg Viewer app
 T.B.D


### Note

#### Error with pulse
```
[RTC|ERROR] (audio_device_pulse_linux.cc:1605): failed to connect context, error=-1
[RTC|ERROR] (audio_device_pulse_linux.cc:145): failed to initialize PulseAudio
[RTC|ERROR] (audio_device_impl.cc:301): Audio device initialization failed.
```
Then: Install pulseaudio if you are sudoer
```sh
sudo apt install pulseaudio
```
In normal user, after installed pulseaudio, run below command
```
pulseaudio --start
```

Note 2: To prevent pulseaudio go to idle mode, add pulseaudio setting:
```
loginctl enable-linger username
```

Copy daemon config to local config
```
cp /etc/pulse/daemon.conf ~/.config/pulse/
```

Change below setting:
```
daemonize = yes
exit-idle-time = -1
```

Then restart pulseaudio service
```
systemctl --user enable pulseaudio
systemctl --user start pulseaudio
```
