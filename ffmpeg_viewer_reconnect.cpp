// ffmpeg_viewer_reconnect.cpp
//
// Variant of ffmpeg_viewer.cpp with two changes aimed at the "old frames
// (20-30s behind) in bad internet" problem on RTMP:
//
//   1. Live-edge latency estimator (rolling two-window minimum offset):
//      latency is measured as an absolute distance behind the live edge
//      (the best-case / most-live packet recently seen), NOT as drift since
//      the first packet. This catches standing latency that the original
//      first-packet anchoring could never see.
//
//   2. In-process reconnect: instead of throwing out to process exit, the
//      decode thread tears the connection down and re-opens it to snap back
//      to the live edge. For RTMP-over-TCP a reconnect is the only thing that
//      actually drains the backlog (dropping frames at the decoder cannot).
//
// The render path is unchanged from the original (latest-frame-only).

#include <cassert>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <stdio.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <opencv2/opencv.hpp>
#include "nlohmann/json.hpp"

#include "src/program_info.hpp"
#include "src/video_renderer.hpp"
#include "src/ffmpeg_renderer.hpp"


using json = nlohmann::json;


static void ffmpeg_log_callback(void* ptr, int level, const char* fmt, va_list vl) {
    // Only process logs that meet your severity requirements
    if (level > av_log_get_level()) return;

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    fprintf(stderr, "FFMPEG: %s", buf);
}

class StreamDecoder {
private:
    std::string stream_url_;
    std::atomic<bool> running_flg_{false};
    std::thread decode_worker_;
    std::condition_variable_any cv_;

    // Video rendereres manager
    std::vector<std::shared_ptr<FFmpegRenderer>> video_renderers_;

    // Protecting mutex for shared variables in decoder
    std::mutex mutex_;
    // Shared latest frame from decoder
    cv::Mat latest_frame_;
    // New frame from decoder
    bool has_new_frame_flg_ = false;
    // Extra info from decode loop
    std::string decode_info_ext_ = "";

    std::atomic<bool> interrupt_flag_{false};

    // FFmpeg state
    AVFormatContext* pFormatCtx = nullptr;
    AVCodecContext* pCodecCtx = nullptr;
    int videoStream = -1;
    AVPacket* packet = nullptr;
    AVFrame* pFrame = nullptr;
    uint8_t* buffer = nullptr;
    AVFrame* pFrameBGR = nullptr;
    struct SwsContext* sws_ctx = nullptr;

    // Reconnect when we fall more than this many ms behind the live edge
    const long reconnect_lag_thrd = 2500;
    // Bound for blocking connect/read I/O (ms) -> turns a dead link into a reconnect
    const long io_timeout_ms = 5000;
    // Wait between reconnect attempts (ms)
    const long reconnect_backoff_ms = 200;
    // Window length for the rolling live-edge minimum estimator (ms)
    const long min_window_ms = 10000;

    // Cached decoded-frame dimensions for the BGR scaler
    int frame_width_ = 0;
    int frame_height_ = 0;

    // (Re)open the input/codec/scaler. Safe to call repeatedly; pair with close_stream().
    bool open_stream() {
        pFormatCtx = avformat_alloc_context();
        if (!pFormatCtx) {
            std::cerr << "Failed to allocate AVFormatContext" << std::endl;
            return false;
        }
        pFormatCtx->interrupt_callback.callback = decode_interrupt_cb;
        pFormatCtx->interrupt_callback.opaque = &interrupt_flag_;

        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "fflags", "nobuffer", 0);
        av_dict_set(&opts, "flags", "low_delay", 0);
        // Bound blocking reads so a dead link can't hang av_read_frame forever
        // (microseconds). This is what lets a stall turn into a reconnect.
        // NOTE: do NOT set the RTMP-protocol "timeout" option here -- it is in
        // seconds and is for listen mode, and breaks the client open.
        av_dict_set(&opts, "rw_timeout", std::to_string(io_timeout_ms * 1000).c_str(), 0);

        if (avformat_open_input(&pFormatCtx, stream_url_.c_str(), NULL, &opts) != 0) {
            av_dict_free(&opts);
            std::cerr << "Could not open stream: " << stream_url_ << std::endl;
            return false;
        }
        av_dict_free(&opts);

        if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
            std::cerr << "Could not get stream info of: " << stream_url_ << std::endl;
            return false;
        }

        videoStream = -1;
        for (unsigned i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStream = i; break;
            }
        }
        if (videoStream < 0) {
            std::cerr << "There is no AVMEDIA_TYPE_VIDEO in stream: " << stream_url_ << std::endl;
            return false;
        }

        const AVCodec* pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
        if (!pCodec) {
            std::cerr << "No decoder found for stream: " << stream_url_ << std::endl;
            return false;
        }
        pCodecCtx = avcodec_alloc_context3(pCodec);
        avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
        pCodecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        pCodecCtx->thread_count = 0;
        pCodecCtx->thread_type = FF_THREAD_SLICE;
        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            std::cerr << "Failed to open codec" << std::endl;
            return false;
        }

        packet = av_packet_alloc();
        pFrame = av_frame_alloc();

        frame_width_ = pCodecCtx->width;
        frame_height_ = pCodecCtx->height;

        // Scaler + BGR24 output buffer
        struct SwsContext* sws_local = sws_getContext(
            pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
            frame_width_, frame_height_, AV_PIX_FMT_BGR24,
            SWS_BILINEAR, NULL, NULL, NULL);
        if (!sws_local) {
            std::cerr << "Failed to create swscale context" << std::endl;
            return false;
        }
        // Exact size FFmpeg wants for BGR24 (32-byte aligned)
        int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, frame_width_, frame_height_, 32);
        buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
        pFrameBGR = av_frame_alloc();
        av_image_fill_arrays(pFrameBGR->data, pFrameBGR->linesize, buffer,
                             AV_PIX_FMT_BGR24, frame_width_, frame_height_, 32);

        // Publish the scaler last, under the lock, so the render loop's readiness
        // check (sws_ctx != nullptr) only ever sees a fully-built pipeline.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sws_ctx = sws_local;
        }

        std::cout << "Got frame HxW: " << frame_height_ << "x" << frame_width_ << std::endl;
        std::cout << "Frame format enum: " << pCodecCtx->pix_fmt << std::endl;
        return true;
    }

    // Tear down everything open_stream() created. Idempotent (all handles nulled),
    // so it is safe to call from both the decode thread and decoder_terminate().
    void close_stream() {
        if (pCodecCtx) avcodec_free_context(&pCodecCtx);
        if (pFormatCtx) avformat_close_input(&pFormatCtx);
        if (pFrame) av_frame_free(&pFrame);
        if (pFrameBGR) av_frame_free(&pFrameBGR);
        if (packet) av_packet_free(&packet);
        if (buffer) { av_free(buffer); buffer = nullptr; }
        // sws_freeContext does NOT null its argument, so null it ourselves (under the
        // lock) to avoid a double-free and to stay consistent with the readiness check.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sws_ctx) { sws_freeContext(sws_ctx); sws_ctx = nullptr; }
        }
        videoStream = -1;
    }

    // Read+decode the currently-open stream until we should reconnect or stop.
    // Returns true if a reconnect is desired, false if we are terminating.
    bool read_decode() {
        AVRational tb = pFormatCtx->streams[videoStream]->time_base;

        int64_t first_dts = AV_NOPTS_VALUE;
        auto stream_start_time = std::chrono::steady_clock::now();

        // Rolling two-window minimum of (real_elapsed - stream_elapsed). The minimum
        // approximates the live edge (best-case packet), so latency is measured as an
        // absolute distance behind live rather than drift-since-first-packet. Two
        // windows keep it tolerant of slow encoder/decoder clock drift while still
        // reacting to genuine lag within one window.
        int64_t cur_min = 0, prev_min = 0;
        bool min_init = false;
        auto window_start = stream_start_time;

        std::string decode_info = "";

        while (running_flg_) {
            if (av_read_frame(pFormatCtx, packet) < 0) {
                // Timeout / I/O error / EOF: reconnect unless we are shutting down.
                return running_flg_;
            }
            if (packet->stream_index != videoStream) {
                av_packet_unref(packet);
                continue;
            }

            auto now = std::chrono::steady_clock::now();

            if (packet->dts != AV_NOPTS_VALUE) {
                if (first_dts == AV_NOPTS_VALUE) {
                    first_dts = packet->dts;
                    stream_start_time = now;
                    window_start = now;
                }

                int64_t real_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - stream_start_time).count();
                int64_t stream_ms = (packet->dts - first_dts) * 1000 * tb.num / tb.den;
                int64_t offset = real_ms - stream_ms; // grows as we fall behind live

                if (!min_init) {
                    cur_min = prev_min = offset;
                    min_init = true;
                } else {
                    auto win_age = std::chrono::duration_cast<std::chrono::milliseconds>(now - window_start).count();
                    if (win_age > min_window_ms) {
                        prev_min = cur_min;
                        cur_min = offset;
                        window_start = now;
                    } else {
                        cur_min = std::min(cur_min, offset);
                    }
                }
                int64_t min_offset = std::min(prev_min, cur_min);
                int64_t latency = offset - min_offset; // ms behind the live edge (~0 when live)

                if (latency > reconnect_lag_thrd) {
                    auto time_t_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    std::stringstream ss;
                    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
                    ss << " [WARN] Live latency " << latency << " ms (> " << reconnect_lag_thrd
                       << " ms). Reconnecting to snap back to the live edge..." << std::endl;
                    std::cerr << ss.str();
                    av_packet_unref(packet);
                    return true; // reconnect
                }
            }

            if (avcodec_send_packet(pCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                    // Convert frame to BGR24 format
                    sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0,
                              frame_height_, pFrameBGR->data, pFrameBGR->linesize);

                    // Wrap the buffer in a cv::Mat safely
                    cv::Mat tmp(frame_height_, frame_width_, CV_8UC3, pFrameBGR->data[0], pFrameBGR->linesize[0]);

                    // Update shared "latest" frame
                    std::lock_guard<std::mutex> lock(mutex_);
                    latest_frame_ = tmp.clone();
                    has_new_frame_flg_ = true;
                    decode_info_ext_ = decode_info;
                    decode_info = "";
                }
            }
            av_packet_unref(packet);
        }
        return false; // running_flg_ went false -> terminating
    }

    // Outer loop: keep a live connection up, reconnecting on lag or I/O failure.
    void decode_loop() {
        // init() already established the first connection.
        bool already_open = true;
        while (running_flg_) {
            if (!already_open) {
                if (!open_stream()) {
                    close_stream();
                    if (!running_flg_) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_backoff_ms));
                    continue;
                }
            }
            already_open = false;

            // Signal the render loop that a stream pipeline is ready.
            cv_.notify_one();

            bool reconnect = false;
            try {
                reconnect = read_decode();
            }
            catch (const std::exception& ex) {
                std::cerr << "Runtime error in read_decode: " << ex.what() << std::endl;
                reconnect = running_flg_;
            }
            catch (...) {
                std::cerr << "Unhandled error in read_decode" << std::endl;
                reconnect = running_flg_;
            }

            close_stream();
            if (!running_flg_) break;
            if (reconnect) std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_backoff_ms));
        }

        std::cout << "End decode_loop" << std::endl;
        running_flg_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            has_new_frame_flg_ = false;
        }
        close_stream();
    } // End of decode_loop

public:
    StreamDecoder(const std::string& stream_url) : stream_url_(stream_url) {}

    ~StreamDecoder() {
        decoder_terminate();
    }

    bool decoder_terminate() {
        // Cancel all running loops before locking to avoid deadlock, and trip the
        // interrupt callback so a blocked av_read_frame / open returns promptly.
        running_flg_ = false;
        interrupt_flag_.store(true);
        if (decode_worker_.joinable()) decode_worker_.join();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            has_new_frame_flg_ = false;
        }
        // The decode thread already freed its handles on exit; close_stream() is
        // idempotent (all handles nulled) so this is safe to call again here.
        close_stream();
        return true;
    }

    bool init() {
        avformat_network_init();
        // Establish the first connection synchronously so init() can fail fast.
        if (!open_stream()) {
            close_stream();
            return false;
        }
        running_flg_ = true;
        decode_worker_ = std::thread(&StreamDecoder::decode_loop, this);
        return true;
    }

    static int decode_interrupt_cb(void *ctx)
    {
        auto flag = static_cast<std::atomic<bool>*>(ctx);
        return flag->load() ? 1 : 0;
    }

    /************ Thread safe of starting render ***************/
    void start_render_loop(std::string config_file, int stream_id=1, bool display=false, bool sampling_mode=false) {
        // Wait for the first track come
        {
            std::unique_lock lock(mutex_);
            if (sws_ctx==nullptr) {
                std::cout<<"Wait for getting track\n";
                // Set timeout 5 seconds for waiting
                if(!cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return sws_ctx!=nullptr; }))
                {
                    throw std::runtime_error("start_render_loop timeout after 5 seconds");
                }
                std::cout<<"Get track success\n";
            }
        }

        std::string render_name = "Track " + std::to_string((int)video_renderers_.size());
        std::shared_ptr<FFmpegRenderer> render = std::make_shared<FFmpegRenderer>(render_name);
        printf("Create render: %s\n", render_name.c_str());
        if (!render->init(config_file, stream_id)) {
            running_flg_ = false;
            std::cerr << "Failed to initialize render: " << render_name << std::endl;
            return;
        }
        render->display_ = display;
        render->sampling_mode_ = sampling_mode;
        video_renderers_.push_back(render);

        cv::Mat frame_to_process;
        bool ready = false;
        std::string decode_info = "";
        while (running_flg_) {
            // Atomic swap the latest frame to local variable for processing
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (has_new_frame_flg_) {
                    frame_to_process = latest_frame_.clone();
                    has_new_frame_flg_ = false;
                    if (decode_info_ext_.length()) {
                        decode_info = decode_info_ext_;
                        decode_info_ext_ = "";
                    }
                    ready = true;
                }
            }

            if (ready && render && running_flg_) {
                ready = false;
                if (decode_info.length()){
                    std::cout<<"Decode info: "<<decode_info<<std::endl;
                    decode_info = "";
                }
                render->on_frame(frame_to_process);
            }

            if (!videostream::VideoRenderer::run_iteration(render)) {
                std::cout<<"Error in render run_iteration" << std::endl;
                break;
            }
        }
        running_flg_ = false;
        std::cout<<"End render loop" << std::endl;
        return;
    }
    /**********************/

};


int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {

    ProgramInfo args = ProgramInfo("FFmpeg Viewer");
    args.parse_arguments(argc, argv);
    args.print_args();

    nlohmann::json config_;
    try{
        std::ifstream f(args.config_file.c_str());
        // Global config
        nlohmann::json g_config = json::parse(f);
        // Copy stream config 1
        config_ = g_config["streams"]["stream_" + std::to_string(args.stream_id)];
    }
    catch(const std::ifstream::failure& e){
        std::cerr<<"Error in read configuration file: "<< args.config_file <<std::endl;
        exit(EXIT_FAILURE);
    }

    if (args.disable_stats) {
        av_log_set_level(AV_LOG_QUIET);
        std::cout<<"Disable FFMPEG log"<<std::endl;
    }
    else {
        std::cout<<"Enable FFMPEG log"<<std::endl;
        av_log_set_level(AV_LOG_PANIC);
        av_log_set_callback(ffmpeg_log_callback);
        if (args.debug) {
            av_log_set_level(AV_LOG_DEBUG);
            std::cout<<"FFMPEG log debug mode"<<std::endl;
        }
    }

    try{
        // Set the credentials and enable the stats
        json streams_config = config_["source"]["entries"];

        std::string url = "";
        for (const auto& item: streams_config) {
            if (item.contains("url")) {
                url = item["url"];
            }
        }

        std::cout<<"Get stream: "<< streams_config <<std::endl;
        std::cout<<"Stream url: "<< url <<std::endl;

        StreamDecoder decoder(url);
        if (!decoder.init()) {
            std::cerr << "Failed to init" << std::endl;
            decoder.decoder_terminate();
            return -1;
        }
        decoder.start_render_loop(args.config_file,
                                    args.stream_id,
                                    args.display,
                                    args.sampling_mode);
        std::cout<<"Start terminate..."<<std::endl;
        decoder.decoder_terminate();
        std::cout<<"Done"<<std::endl;
    }
    catch (...) {
        std::cerr<<"Unhandled exception in play stream"<<std::endl;
    }

    std::cout<<"End Viewer"<<std::endl;
    return 0;
}
