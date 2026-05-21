
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <fstream>
#include <unistd.h>

#include <exception>
#include <typeinfo>
#include <stdexcept>

#include <opencv2/opencv.hpp>
#include "nlohmann/json.hpp"
// #include <unistd.h>

#include "zmq.hpp"
#include "ffmpeg_renderer.hpp"

using json = nlohmann::json;



FFmpegRenderer::FFmpegRenderer(const std::string &title) : videostream::VideoRenderer(title) {

}

FFmpegRenderer::~FFmpegRenderer() {
}



void FFmpegRenderer::on_frame(const cv::Mat& frame) {
    // printf("[%s] On frame CV video renderer:\n", title_.c_str());
    if (!mutex_.try_lock())
        return;

    std::chrono::duration<float> time_diff;
    try{
        // Update frame dimension and frame data buffer size
        if (width_ != frame.cols || height_ != frame.rows) {
            printf("Got new image size: %d x %d, size: %d\n", frame.cols, frame.rows, frame.channels());
            width_ = frame.cols;
            height_ = frame.rows;
        }
        if(!has_frame_) printf("Got first frame ****** \n");
        image_bgr_ = frame.clone();
        has_frame_ = true;
        // Update frame buffer
        
        
        // Calculate frame FPS
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> time_diff = now - image_tp_;
        image_tp_ = now;
        if (frame_fps_ < 10 || frame_fps_ > 40)
        {
            // Fast update frame FPS
            frame_fps_ = 1.0f/time_diff.count();
        }
        else {
            // Moving average frame FPS
            frame_fps_ = 0.4f*frame_fps_ + 0.6f/time_diff.count();
        }

    }
    catch (std::exception &e){
        std::cerr<<"Error in VideoRenderer::on_frame: "<< e.what() <<std::endl;
        exit(EXIT_FAILURE);
    }

    mutex_.unlock();

    // Print report
    time_diff = std::chrono::high_resolution_clock::now() - report_tp_;
    if (time_diff.count() >= report_dur_)
    {
        report_tp_ = std::chrono::high_resolution_clock::now();
        // 
        auto time_t_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        printf("[%s] image FPS: %.2f, %d x %d\n", ss.str().c_str(), frame_fps_, width_, height_);
        fflush(stdout); // Forces the text out of the buffer and into PM2 logs
    }
}
