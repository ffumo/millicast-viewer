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


class StreamDecoder {
private:
    std::string stream_url_;
    std::atomic<bool> running_flg_{false};
    std::thread decode_worker_;
    std::thread onframe_worker_;
    std::mutex mutex_;
    // std::mutex 
    std::vector<std::shared_ptr<FFmpegRenderer>> video_renderers_;

    // Shared latest frame
    cv::Mat latest_frame_;
    // cv::Mat shared_frame_;
    bool has_new_frame_flg_ = false;

    // FFmpeg state
    AVFormatContext* pFormatCtx = nullptr;
    AVCodecContext* pCodecCtx = nullptr;
    int videoStream = -1;

    // Callback function pointer
    // void (*on_frame_callback)(const cv::Mat&) = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* pFrame = nullptr;
    struct SwsContext* sws_ctx = nullptr;

    void decode_loop() {
        try {
            packet = av_packet_alloc();
            pFrame = av_frame_alloc();
            
            // Setup Scaler for BGR24
            sws_ctx = sws_getContext(
                pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_BGR24,
                SWS_BILINEAR, NULL, NULL, NULL);

            while (running_flg_) {
                // Read packets as fast as possible to keep buffer empty
                if (av_read_frame(pFormatCtx, packet) >= 0) {
                    if (packet->stream_index == videoStream) {
                        if (avcodec_send_packet(pCodecCtx, packet) == 0) {

                            while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                                // Convert directly to a temporary Mat
                                cv::Mat tmp(pCodecCtx->height, pCodecCtx->width, CV_8UC3);
                                uint8_t* dest[] = { tmp.data };
                                int destLinesize[] = { (int)tmp.step };
                                sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, 
                                        pCodecCtx->height, dest, destLinesize);
                                
                                // 2. Update shared "Latest" frame (Atomic Swap)
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    latest_frame_ = tmp; // Shallow copy of cv::Mat is fine here because tmp is local
                                    has_new_frame_flg_ = true;
                                }

                            }

                        }
                    }
                    av_packet_unref(packet);
                }

                // Thread-safe update of the "latest" frame
                // if (has_new_frame_flg_) {
                //     std::lock_guard<std::mutex> lock(mutex_);
                //     // latest_frame_ = tmp.clone(); // Shallow copy isn't enough, we need the data
                //     // Update frame to renderer, 
                //     has_new_frame_flg_ = false;
                //     video_renderers_.back()->on_frame(latest_frame_);
                //     // onframe_worker_ = std::thread(&StreamDecoder::on_frame, this);
                // }

                // // 1. DRAIN ALL PENDING PACKETS
                // // This clears the network buffer so you are always at the "head"
                // while (av_read_frame(pFormatCtx, packet) >= 0) {
                //     if (packet->stream_index == videoStream) {
                //         avcodec_send_packet(pCodecCtx, packet);
                //     }
                //     av_packet_unref(packet);
                    
                //     // Break after reading a few packets to check for a decoded frame
                //     // This prevents being stuck in an infinite read loop
                //     if (pFormatCtx->pb && pFormatCtx->pb->buffer_size < 1024) break; 
                // }

                // // 2. GET THE LATEST DECODED FRAME
                // // We only care about the most recent frame the decoder can give us
                // while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                //     // Convert to cv::Mat only for the LAST frame received
                //     sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, 
                //             pCodecCtx->height, dest, destLinesize);
                // }
            }
        }
        catch (...){
            std::cerr<<"Error in decode_loop"<<std::endl;
        }

        running_flg_ = false;
        av_frame_free(&pFrame);
        av_packet_free(&packet);
        sws_freeContext(sws_ctx);

    }

    // void on_frame() {
    //     std::lock_guard<std::mutex> lock(mutex_);
    //     video_renderers_.back()->on_frame(latest_frame_);
    // }

public:
    StreamDecoder(const std::string& stream_url) : stream_url_(stream_url) {}

    ~StreamDecoder() {
        running_flg_ = false;
        if (decode_worker_.joinable()) decode_worker_.join();
        avcodec_free_context(&pCodecCtx);
        avformat_close_input(&pFormatCtx);
        av_frame_free(&pFrame);
        av_packet_free(&packet);
        sws_freeContext(sws_ctx);
    }

    bool init() {
        avformat_network_init();
        
        //  set AV options
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "fflags", "nobuffer", 0);
        av_dict_set(&opts, "flags", "low_delay", 0);

        if (avformat_open_input(&pFormatCtx, stream_url_.c_str(), NULL, &opts) != 0) {
            std::cerr << "Could not open stream: " << stream_url_ << std::endl;
            return false;
        }

        if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
            std::cerr << "Could not get stream info of: " << stream_url_ << std::endl;
            return false;
        }
        
        // 
        for (int i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStream = i; break;
            }
        }

        if (videoStream < 0) {
            std::cerr << "There is no AVMEDIA_TYPE_VIDEO in stream: " << stream_url_ << std::endl;
            return false;
        }

        const AVCodec* pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
        pCodecCtx = avcodec_alloc_context3(pCodec);
        avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
        
        // Disable frame reordering (removes the delay caused by B-frames)
        // pCodecCtx->has_b_frames = 0; 

        // Low Latency Decoder Flags
        pCodecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        // For H.264, use one thread to avoid frame-threading latency (optional)
        pCodecCtx->thread_count = 1; 

        if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            std::cerr << "Failed to set Decoder Flag"<< std::endl;
            return false;
        }
        
        std::cout<<"Got frame HxW: "<<pCodecCtx->height<<"x"<<pCodecCtx->width<<std::endl;
        std::cout<<"Frame format enum: "<<pCodecCtx->pix_fmt<<std::endl;

        running_flg_ = true;
        decode_worker_ = std::thread(&StreamDecoder::decode_loop, this);
        return true;
    }

    // void set_callback(void (*cb)(const cv::Mat&)) {
    //     on_frame_callback = cb;
    // }

    // Thread safe of starting render
    void start_render_loop(std::string config_file, int stream_id=1, bool display=false) {
        // Wait for the first track come
        // {
        //     std::unique_lock lock(mutex_);
        //     if (tracks_to_render_.empty()) {
        //         std::cout<<"Wait for getting track\n";
        //         // cv_.wait(lock, [this]() { return !tracks_to_render_.empty(); });
        //         // Set timeout 7 seconds for waiting 
        //         if(!cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return !tracks_to_render_.empty(); }))
        //         {
        //             throw std::runtime_error("start_render_loop timeout after 7 seconds");
        //         }
        //         std::cout<<"Get track success\n";
        //     }
        // }
        std::string render_name = "Track " + std::to_string((int)video_renderers_.size());
        std::shared_ptr<FFmpegRenderer> render = std::make_shared<FFmpegRenderer>(render_name);
        printf("Create render: %s\n", render_name.c_str());
        render->init(config_file, stream_id);
        render->display_ = display;
        video_renderers_.push_back(render);

        cv::Mat frame_to_process;
        bool ready = false;
        while (running_flg_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (has_new_frame_flg_) {
                    frame_to_process = latest_frame_.clone();
                    has_new_frame_flg_ = false;
                    ready = true;
                }
            }

            if (ready && render) {
                render->on_frame(frame_to_process);
            }

            videostream::VideoRenderer::run_iteration(render);
        }
    }

    // This is the iteration function you requested
    // void run_iteration() {
    //     cv::Mat frame_to_process;
    //     bool ready = false;

    //     {
    //         std::lock_guard<std::mutex> lock(frame_mutex);
    //         if (has_new_frame) {
    //             frame_to_process = latest_frame;
    //             has_new_frame = false;
    //             ready = true;
    //         }
    //     }

    //     if (ready && on_frame_callback) {
    //         on_frame_callback(frame_to_process);
    //     }
    // }

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


    try{
        // Set the credentials and enable the stats
        json stream_config = config_["source"]["entries"][0];
        std::string url = stream_config["url"];
        std::cout<<"Get stream: "<< stream_config <<std::endl;
        std::cout<<"Stream url: "<< url <<std::endl;

        StreamDecoder decoder(url);
        if (!decoder.init()) {
            std::cerr << "Failed to init" << std::endl;
            return -1;
        }
        decoder.start_render_loop(args.config_file, args.stream_id, args.display);
    }
    catch (...) {
        std::cerr<<"Unhandled exception in play stream"<<std::endl;
    }

    return 0;
}
