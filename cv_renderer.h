#ifndef CV_RENDERER_H
#define CV_RENDERER_H

#include <millicast-sdk/renderer.h>

#include <memory>
#include <atomic>
#include <mutex>
#include "zmq.hpp"
#include "nlohmann/json.hpp"
#include <opencv2/opencv.hpp>
#include <chrono>



class VideoRenderer : public millicast::VideoRenderer {
    // std::unique_ptr<uint8_t[]> image;
    // std::unique_ptr<uchar[]> _image;
    std::vector<uchar> image_data_;
    cv::Mat image_bgr_;

    std::mutex mutex_;

    zmq::context_t zmq_ctx_;
    std::string zmq_address_;
    std::string zmq_topic_;
    std::shared_ptr<zmq::socket_t> zmq_sock_p_;
    nlohmann::json config_;

    int width_;
    int height_;
    std::chrono::high_resolution_clock::time_point image_tp_;
    // void init_();
    std::chrono::high_resolution_clock::time_point send_tp_;

public:
    std::string title_;
    std::atomic_bool display_ = {false};

    VideoRenderer(const std::string &title);
    ~VideoRenderer();

    void init() override;

    /*
    Init renderer with configuration file
    @param config_file Configuration file that contain streams config in json format
    @param stream_id Stream id. Default: 1
    */
    void init(const std::string &config_file, int stream_id=1);
    void on_frame(const millicast::VideoFrame& frame) override;
    static bool run_iteration(const std::shared_ptr<VideoRenderer>& render);

    // utilities methods
    cv::Mat get_image_bgr();
    void public_image();

    void destroy();
    void on_destroyed();
    void on_redraw();
    void draw();
    bool set_window_size(int w, int h);
};

#endif /* LINUX_RENDERER_H */
