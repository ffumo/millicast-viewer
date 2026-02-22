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

    // zmq::context_t zmq_ctx_;
    zmq::context_t * zmq_ctx_p_;
    std::string zmq_address_;
    std::string zmq_topic_;
    std::shared_ptr<zmq::socket_t> zmq_sock_p_;
    // zmq::socket_t * zmq_sock_p_;
    nlohmann::json config_;

    int width_;
    int height_;
    std::chrono::high_resolution_clock::time_point image_tp_;
    // void init_();
    std::chrono::high_resolution_clock::time_point send_tp_;
    std::chrono::high_resolution_clock::time_point report_tp_;

    float frame_fps_;

    // Timestamp at begining of app
    std::chrono::high_resolution_clock::time_point viewer_start_tp_;
    // Flag is set when the first frame had arrived
    bool has_frame_ = false;
    // Report duration is 5s
    float report_dur_ = 5.0f;
    // No Frame Timeout is 5s, after then exit viewer. Set 0 to disable
    float noframe_timeout_ = 5.0f;
    // Starting app timeout, if has no frame after timeout, then exit viewer. Set 0 to disable
    float viewer_start_timeout_ = 5.0f;

public:
    std::string title_;
    std::atomic_bool display_ = {false};

    VideoRenderer(const std::string &title, zmq::context_t * zmq_ctx);
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
    std::chrono::high_resolution_clock::time_point get_image_tp();
    void public_image();

    void destroy();
    void on_destroyed();
    void on_redraw();
    void draw();
    bool set_window_size(int w, int h);
};

#endif /* LINUX_RENDERER_H */
