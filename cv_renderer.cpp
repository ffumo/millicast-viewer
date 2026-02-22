
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

VideoRenderer::VideoRenderer(const std::string &title, zmq::context_t * zmq_ctx) {
    title_ = title;
    width_ = 0;
    height_ = 0;
    has_frame_ = false;
    zmq_ctx_p_ = zmq_ctx;
    printf("Create new CV video renderer: %s\n", title.c_str());
    image_tp_ = std::chrono::high_resolution_clock::now();
    report_tp_ = std::chrono::high_resolution_clock::now();
    viewer_start_tp_ = std::chrono::high_resolution_clock::now();
}

VideoRenderer::~VideoRenderer() {
}


bool VideoRenderer::set_window_size(int w, int h) {
    return false;
}

void VideoRenderer::init() {
    zmq_sock_p_ = std::make_shared<zmq::socket_t>(*zmq_ctx_p_, zmq::socket_type::pub);
    if(!config_.empty()){
        zmq_address_ = config_["zmq"]["address"];
        if(config_["zmq"].contains("topic")){
            zmq_topic_ = config_["zmq"]["topic"];
        }
        else{
            zmq_topic_ = "";
        }

        if(config_.contains("report_duration"))
        {
            report_dur_ = config_["report_duration"];
        }

        if(config_.contains("noframe_timeout"))
        {
            noframe_timeout_ = config_["noframe_timeout"];
        }

        if(config_.contains("viewer_start_timeout"))
        {
            viewer_start_timeout_ = config_["viewer_start_timeout"];
        }
    }
    else{
        zmq_address_ = "tcp://localhost:50550";
        zmq_topic_ = "";
    }
    printf("Create ZMQ at: %s\n", zmq_address_.c_str());
    
    int sndhwm = 300;
    zmq_sock_p_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    int conflate = 1;
    zmq_sock_p_->setsockopt(ZMQ_CONFLATE, &conflate, sizeof(conflate));
    int linger = 0;
    zmq_sock_p_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

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
        std::cerr<<"Error in read configuration file: " << e.what() << "\n Configuration: "<< config_file <<std::endl;
        exit(EXIT_FAILURE);
    }
}


void VideoRenderer::on_frame(const millicast::VideoFrame& frame) {
    // printf("[%s] On frame CV video renderer:\n", title_.c_str());
    if (!mutex_.try_lock())
        return;

    std::chrono::duration<float> time_diff;
    try{
        // Update frame dimension and frame data buffer size
        if (width_ != frame.width() || height_ != frame.height()) {
            printf("Got new image size: %d x %d, size: %u\n", frame.width(), frame.height(), frame.size(millicast::VideoType::I420));
            width_ = frame.width();
            height_ = frame.height();
            image_data_.resize(width_ * height_ * 3/2);
        }
        if(!has_frame_) printf("Got first frame ****** \n");
        has_frame_ = true;
        // Update frame buffer
        frame.get_buffer(millicast::VideoType::I420, &image_data_[0]);
        
        // Calculate frame FPS
        std::chrono::duration<float> time_diff = std::chrono::high_resolution_clock::now() - image_tp_;
        image_tp_ = std::chrono::high_resolution_clock::now();
        if (frame_fps_ < 10 || frame_fps_ > 40)
        {
            // Fast update frame FPS
            frame_fps_ = 1.0f/time_diff.count();
        }
        else {
            // Moving average frame FPS
            frame_fps_ = 0.4f*frame_fps_ + 0.6f/time_diff.count();
        }

        // Copy frame buffer to CV image mat
        // printf("[%s] Create cv mat: %ld\n", title_.c_str(), image_data_.size());
        cv::Mat myuv(height_ + height_/2, width_, CV_8UC1, &image_data_[0], cv::Mat::AUTO_STEP);
        image_bgr_ = cv::Mat(cv::Size(width_, height_), CV_8UC3);
        // printf("[%s] Convert color\n", title_.c_str());
        cv::cvtColor(myuv, image_bgr_, cv::COLOR_YUV2BGR_I420);
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
    }
}

cv::Mat VideoRenderer::get_image_bgr()
{
    std::lock_guard lock(mutex_);
    return image_bgr_.clone();
}

std::chrono::high_resolution_clock::time_point VideoRenderer::get_image_tp()
{
    std::lock_guard lock(mutex_);
    return image_tp_;
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
    
    if (render->has_frame_) {
        auto _image_tp = render->get_image_tp();
        std::chrono::duration<float> time_diff = std::chrono::high_resolution_clock::now() - _image_tp;
        // If noframe_timeout_ is set, then check timeout
        if ((render->noframe_timeout_ > 0.5f) && (time_diff.count() > render->noframe_timeout_)) {
            // throw std::runtime_error("Error no frame after: " + std::to_string(time_diff.count()));
            printf("Error no frame after: %f seconds\n", time_diff.count());
            // Return false to cancel TrackManager in render loop
            return false;
        }
    }
    else {
        std::chrono::duration<float> time_diff = std::chrono::high_resolution_clock::now() - render->viewer_start_tp_;
        if ((render->viewer_start_timeout_ > 0.5f) && (time_diff.count() > render->viewer_start_timeout_)) {
            // throw std::runtime_error("Error no frame after: " + std::to_string(time_diff.count()));
            printf("Error no frame after: %f seconds\n", time_diff.count());
            // Return false to cancel TrackManager in render loop
            return false;
        }
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
