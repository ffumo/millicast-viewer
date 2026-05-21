
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
#include "video_renderer.hpp"

using json = nlohmann::json;


using namespace videostream;

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
    has_frame_ = false;
    printf("Create new CV video renderer: %s\n", title.c_str());
    image_tp_ = std::chrono::high_resolution_clock::now();
    report_tp_ = std::chrono::high_resolution_clock::now();
    viewer_start_tp_ = std::chrono::high_resolution_clock::now();
    sampling_tp_ = std::chrono::high_resolution_clock::now();
}

VideoRenderer::~VideoRenderer() {
}


bool VideoRenderer::set_window_size(int w, int h) {
    return false;
}

// void VideoRenderer::init() {
    
// }


void VideoRenderer::init(const std::string &config_file, int stream_id) {
    try{
        std::ifstream f(config_file.c_str());
        // Global config
        nlohmann::json g_config = json::parse(f);
        // Copy stream config 1
        config_ = g_config["streams"]["stream_1"];
        // init();
        zmq_sock_p_ = std::make_shared<zmq::socket_t>(zmq_ctx_, zmq::socket_type::pub);

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
        zmq_sock_p_->bind(zmq_address_.c_str());

        // Set sending timepoint after image timepoint, avoid send empty image
        send_tp_ = std::chrono::high_resolution_clock::now();
    }
    catch(const std::ifstream::failure& e){
        std::cerr<<"Error in read configuration file: " << e.what() << "\n Configuration: "<< config_file <<std::endl;
        exit(EXIT_FAILURE);
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

void VideoRenderer::public_image(){
    json j;
    std::string s;
    std::vector<uchar> buffer;
    {
        std::lock_guard lock(mutex_);
        if (image_bgr_.empty()) return;
        if (send_tp_ >= image_tp_) return;
        
        cv::imencode(".jpg", image_bgr_, buffer);
        send_tp_ = image_tp_;
    }
    zmq_sock_p_->send((const void*)buffer.data(), buffer.size(), ZMQ_NOBLOCK);
}


std::mutex VideoRenderer::iteration_mutex_;
bool VideoRenderer::run_iteration(const std::shared_ptr<VideoRenderer>& render) {
    std::lock_guard lock(iteration_mutex_);
    std::thread _zmq_thread = std::thread(&VideoRenderer::public_image, render);

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
    
    std::chrono::duration<float> time_diff;
    // Sampling image
    time_diff = std::chrono::high_resolution_clock::now() - render->sampling_tp_;

    if (render->sampling_mode_ && (time_diff.count() >= render->sampling_dur_))
    {
        render->sampling_tp_ = std::chrono::high_resolution_clock::now();
        // 
        cv::Mat img = render->get_image_bgr();
        if (!img.empty()) {
            auto time_t_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

            std::stringstream file_name_ss;
            file_name_ss << render->title_<<"_sample_"<<std::put_time(std::localtime(&time_t_now), "%Y%m%d_%H%M%S")<<".jpg";
            cv::imwrite(file_name_ss.str(), img);
            render->sampling_files.push_back(file_name_ss.str());
            if (render->sampling_files.size() > 5) {
                if (!std::remove(render->sampling_files.front().c_str())) {
                    std::cout << "Error in delete file: " << render->sampling_files.front() << std::endl;
                }
                render->sampling_files.erase(render->sampling_files.begin());
            }

            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
            printf("[%s] Saved sample image: %s - %d x %d\n", ss.str().c_str(), file_name_ss.str().c_str(), img.rows, img.cols);
        }

    }

    if (render->has_frame_) {
        auto _image_tp = render->get_image_tp();
        time_diff = std::chrono::high_resolution_clock::now() - _image_tp;
        // If noframe_timeout_ is set, then check timeout
        if ((render->noframe_timeout_ > 0.5f) && (time_diff.count() > render->noframe_timeout_)) {
            // throw std::runtime_error("Error no frame after: " + std::to_string(time_diff.count()));
            printf("Error no frame after: %f seconds\n", time_diff.count());
            // Return false to cancel TrackManager in render loop
            return false;
        }
    }
    else {
        time_diff = std::chrono::high_resolution_clock::now() - render->viewer_start_tp_;
        if ((render->viewer_start_timeout_ > 0.5f) && (time_diff.count() > render->viewer_start_timeout_)) {
            // throw std::runtime_error("Error no frame after: " + std::to_string(time_diff.count()));
            printf("Error no frame after: %f seconds\n", time_diff.count());
            // Return false to cancel TrackManager in render loop
            return false;
        }
    }
    fflush(stdout); 

    // render->public_image();
    if (_zmq_thread.joinable()) {
        _zmq_thread.join();
    }

    return true;
}

void VideoRenderer::destroy() {
}

void VideoRenderer::on_destroyed(){

}

void VideoRenderer::on_redraw() {
}

void VideoRenderer::draw() {
}
