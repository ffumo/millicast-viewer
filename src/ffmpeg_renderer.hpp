#ifndef __MILLICAST_RENDERER_H__
#define __MILLICAST_RENDERER_H__


#include <memory>
#include <atomic>
#include <mutex>
#include "zmq.hpp"
#include "nlohmann/json.hpp"
#include <opencv2/opencv.hpp>
#include <chrono>
#include "video_renderer.hpp"



class FFmpegRenderer : public videostream::VideoRenderer {

public:
    std::string title_;

    FFmpegRenderer(const std::string &title);
    ~FFmpegRenderer();

    // void init() {} ;
    using videostream::VideoRenderer::init;
    /*
    Init renderer with configuration file
    @param config_file Configuration file that contain streams config in json format
    @param stream_id Stream id. Default: 1
    */
    // void init(const std::string &config_file, int stream_id=1);
    void on_frame(const cv::Mat& frame);
    // static bool run_iteration(const std::shared_ptr<millicast::VideoRenderer>& render);

    // utilities methods
    // cv::Mat get_image_bgr();
    // std::chrono::high_resolution_clock::time_point get_image_tp();
    // void public_image();

    // void destroy();
    // void on_destroyed();
    // void on_redraw();
    // void draw();
    // bool set_window_size(int w, int h);
};

#endif /* __MILLICAST_RENDERER_H__ */
