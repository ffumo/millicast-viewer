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

- Download thirdparty libraries<br/>
    
    Please consider your system architecture (Darwin or Linux)
    ```
    echo "$(uname)"
    ```

    - [Millicast SDK](https://github.com/millicast/millicast-native-sdk/releases) for stream subscription [Darwin]
        ```shell
        wget https://github.com/millicast/millicast-native-sdk/releases/download/v2.0.0/millicast-native-sdk-2.0.0-macos-arm64.zip -O 3rd/millicast.zip

        unzip 3rd/millicast.zip -d 3rd && mv 3rd/usr lib/millicast
        ```

    - [OpenCV](https://docs.opencv.org/4.x/d7/d9f/tutorial_linux_install.html) for image processing
        ```shell
        wget -O 3rd/opencv.zip https://github.com/opencv/opencv/archive/4.10.0.zip

        wget -O 3rd/opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/4.10.0.zip

        unzip 3rd/opencv.zip -d 3rd

        unzip 3rd/opencv_contrib.zip -d 3rd

        sh scripts/opencv_cmake_install.sh
        ```

    - [ZeroMQ](https://github.com/zeromq/libzmq/releases/tag/v4.3.5) for re-publishing image to python
        ```shell
        wget -O 3rd/zeromq.zip https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.zip

        unzip 3rd/zeromq.zip -d 3rd

        sh scripts/zeromq_cmake_install.sh
        ```

    - [CppZMQ](https://github.com/zeromq/cppzmq/releases/tag/v4.10.0) C++ wrapping of ZeroMQ
        ```shell
        wget -O 3rd/cppzmq.zip https://github.com/zeromq/cppzmq/archive/refs/tags/v4.10.0.zip
        
        unzip 3rd/cppzmq.zip -d 3rd

        sh scripts/zmqcpp_cmake_install.sh
        ```

    - [Json](https://github.com/nlohmann/json/releases/tag/v3.11.3) for parsing JSON configuration file
        ```shell
        wget -O 3rd/json.zip https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip

        unzip 3rd/json.zip "include/nlohmann/*" -d 3rd/json/
        ```

    - [Argparse](https://github.com/p-ranav/argparse/releases/tag/v3.1) for parsing commandline arguments
        ```shell
        wget -O 3rd/argparse.zip https://github.com/p-ranav/argparse/archive/refs/tags/v3.1.zip
        unzip -j 3rd/argparse.zip argparse-3.1/include/argparse/argparse.hpp -d 3rd/argparse/
        ```

### Build app

Run scripts in [scripts](scripts) directory for build libs (except final build script [build.sh](script/build.sh))

```
source scripts/source_lib.sh

sh build.sh
```

### Run app

After build success, app is located at [millicast_viewer](build/millicast_viewer).<br/>
To run app, you have to source dependent libraries [source_lib.sh](script/source_lib.sh)
```shell
source scripts/source_lib.sh

./build/millicast_viewer
```
App available options:
-  -c, --config: configuration file [required]
-  --id :stream id in configuration file [nargs=0..1] [default: 1]
-  -d, --display: enable display 

Configuration file contains stream information. Please refer to [sample_config.json](config/sample_config.json)


