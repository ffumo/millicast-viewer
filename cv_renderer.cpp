
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <fstream>
#include <opencv2/opencv.hpp>
#include "nlohmann/json.hpp"
// #include <unistd.h>

#include "zmq.hpp"
#include "cv_renderer.h"

using json = nlohmann::json;


std::string GetStringCurrentTime(std::chrono::system_clock::time_point tp) {
    // Convert to local time
    auto localTime = std::chrono::system_clock::to_time_t(tp);

    // Format the timestamp as a string
    std::stringstream ss;
    ss << std::put_time(std::localtime(&localTime), "%F_%H-%M-%S");
    return ss.str();
}

VideoRenderer::VideoRenderer(const std::string &title) {
    title_ = title;
    width_ = 0;
    height_ = 0;
    printf("Create new CV video renderer: %s\n", title.c_str());
    image_tp_ = std::chrono::high_resolution_clock::now();
}

VideoRenderer::~VideoRenderer() {
}


bool VideoRenderer::set_window_size(int w, int h) {
    return false;
}

void VideoRenderer::init() {
    zmq_sock_p_ = std::make_shared<zmq::socket_t>(zmq_ctx_, zmq::socket_type::pub);
    if(!config_.empty()){
        zmq_address_ = config_["zmq"]["address"];
        zmq_topic_ = config_["zmq"]["topic"];
    }
    else{
        zmq_address_ = "tcp://localhost:50550";
        zmq_topic_ = "";
    }
    zmq_sock_p_->bind(zmq_address_.c_str());

    // Set sending timepoint after image timepoint, avoid send empty image
    send_tp_ = std::chrono::high_resolution_clock::now();
}


void VideoRenderer::init(const std::string &config_file, int stream_id) {
    try{
        std::ifstream f(config_file.c_str());
        // Global config
        nlohmann::json g_config = json::parse(f);
        // Copy stream config 1
        config_ = g_config["streams"]["stream_1"];
        init();
    }
    catch(const std::ifstream::failure& e){
        std::cerr<<"Error in read configuration file: "<< config_file <<std::endl;
        exit(EXIT_FAILURE);
    }
}


void VideoRenderer::on_frame(const millicast::VideoFrame& frame) {
    // printf("[%s] On frame CV video renderer:\n", title_.c_str());
    if (!mutex_.try_lock())
        return;

    if (width_ != frame.width() || height_ != frame.height()) {
        printf("Got new image size: %d x %d, size: %u\n", frame.width(), frame.height(), frame.size(millicast::VideoType::I420));
        width_ = frame.width();
        height_ = frame.height();
        image_data_.resize(width_ * height_ * 3/2);
    }
    frame.get_buffer(millicast::VideoType::I420, &image_data_[0]);
    std::chrono::duration<double> time_diff = std::chrono::high_resolution_clock::now() - image_tp_;
    image_tp_ = std::chrono::high_resolution_clock::now();
    printf("Get image buffer FPS: %.2lf\n", 1.0/time_diff.count());

    // printf("[%s] Create cv mat: %ld\n", title_.c_str(), image_data_.size());
    cv::Mat myuv(height_ + height_/2, width_, CV_8UC1, &image_data_[0], cv::Mat::AUTO_STEP);
    image_bgr_ = cv::Mat(cv::Size(width_, height_), CV_8UC3);
    // printf("[%s] Convert color\n", title_.c_str());
    cv::cvtColor(myuv, image_bgr_, cv::COLOR_YUV2BGR_I420);

    mutex_.unlock();
}

cv::Mat VideoRenderer::get_image_bgr()
{
    std::lock_guard lock(mutex_);
    return image_bgr_.clone();
}

bool VideoRenderer::run_iteration(const std::shared_ptr<VideoRenderer>& render) {
    if (render->display_){
        // printf("[%s] Run iteration data size: %ld\n", render->title_.c_str(), render->image_data_.size());
        cv::Mat img = render->get_image_bgr();
        if (img.empty()) {
            return true;
        }

        cv::imshow("Stream", img);
        int k = cv::waitKey(1);
        if (k == 'q') {
            std::exit(EXIT_SUCCESS);
        }
        else if (k == 'c') {
            std::string img_name = GetStringCurrentTime(std::chrono::system_clock::now()) + ".jpg";
            cv::imwrite(img_name, img);
            printf("Saved image to: %s\n", img_name.c_str());
        }
    }
    else{
        // usleep(1000);
        // int k = cv::waitKey(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    render->public_image();
    return true;
}

void VideoRenderer::destroy() {
}

void VideoRenderer::on_destroyed(){

}

void VideoRenderer::public_image(){
    json j;
    std::string s;
    std::vector<uchar> buffer;
    {
        std::lock_guard lock(mutex_);
        if (image_data_.size() == 0) return;
        if (send_tp_ >= image_tp_) return;
        cv::imencode(".jpg", image_bgr_, buffer);
        send_tp_ = image_tp_;
    }
    zmq_sock_p_->send((const void*)buffer.data(), buffer.size(), ZMQ_NOBLOCK);
}

void VideoRenderer::on_redraw() {
}

void VideoRenderer::draw() {
}
