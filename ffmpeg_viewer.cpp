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
    std::condition_variable_any cv_;
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
    uint8_t* buffer = nullptr;
    AVFrame* pFrameBGR = nullptr;
    struct SwsContext* sws_ctx = nullptr;

    // Decode loop function
    void decode_loop() {
        try {
            packet = av_packet_alloc();
            pFrame = av_frame_alloc();
            
            // 1. Setup Scaler for BGR24
            sws_ctx = sws_getContext(
                pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_BGR24,
                SWS_BILINEAR, NULL, NULL, NULL);

            int width = pCodecCtx->width;
            int height = pCodecCtx->height;
            
            // 2. Prepare BGR24 frame buffer
            // Exact size FFmpeg wants for BGR24
            int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, width, height, 32); // 32-byte align
            // Prepare BGR24 buffer data
            buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
            // Map this buffer to a temporary frame for swscale
            pFrameBGR = av_frame_alloc();
            av_image_fill_arrays(pFrameBGR->data, pFrameBGR->linesize, buffer, 
                                AV_PIX_FMT_BGR24, width, height, 32);
            
            // 3. Prevent drifting
            auto stream_start_time = std::chrono::steady_clock::now();
            int64_t first_dts = AV_NOPTS_VALUE;
            bool frame_drifted_flag = false;
            AVRational stream_time_base = pFormatCtx->streams[videoStream]->time_base;

            // Notify to start iteration loop
            cv_.notify_one();
            while (running_flg_) {
                // Read packets as fast as possible to keep buffer empty
                if (av_read_frame(pFormatCtx, packet) >= 0) {
                    if (packet->stream_index == videoStream) {
                        // Track timestamps to identify stream drift
                        if (first_dts == AV_NOPTS_VALUE && packet->dts != AV_NOPTS_VALUE) {
                            first_dts = packet->dts;
                            stream_start_time = std::chrono::steady_clock::now();
                        }

                        if ((first_dts != AV_NOPTS_VALUE) && (packet->dts != AV_NOPTS_VALUE)) {
                            // Calculate how long the stream has been playing in real life vs stream timestamps
                            auto elapsed_real = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - stream_start_time).count();
                            
                            // Convert stream timebase to milliseconds
                            int64_t elapsed_stream = (packet->dts - first_dts) * 1000 * stream_time_base.num / stream_time_base.den;
                            
                            auto time_diff = elapsed_real - elapsed_stream;
                            // Danger lag detected
                            if (time_diff > 5000) {
                                std::cerr<<"[ERR] Critical lag detected: " << time_diff <<" ms. Reconnecting stream to reset hardware buffers..."<< std::endl;
                                throw std::runtime_error("[ERR] Critical lag detected. Reconnecting stream to reset hardware buffers...");
                            }
                            // DRIFT DETECTION: If stream time is > 500ms behind real time, drop it!
                            else if (time_diff > 500) {
                                frame_drifted_flag = true;
                            }
                            else if (time_diff < 100) {
                                frame_drifted_flag = false;
                            }
                        }

                        // Crucial: Only drop safely if it's NOT a keyframe (to avoid breaking the group of pictures)
                        if (frame_drifted_flag && !(packet->flags & AV_PKT_FLAG_KEY)) {
                            av_packet_unref(packet);
                            continue; // Skip decoding this frame entirely
                        }

                        if (avcodec_send_packet(pCodecCtx, packet) == 0) {

                            while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                                // Convert frame to BGR24 format
                                sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, 
                                        height, pFrameBGR->data, pFrameBGR->linesize);

                                // Wrap the buffer in a cv::Mat safely
                                cv::Mat tmp(height, width, CV_8UC3, pFrameBGR->data[0], pFrameBGR->linesize[0]);

                                // Update shared "Latest" frame (Atomic Swap)
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    // latest_frame_ = tmp; // Shallow copy of cv::Mat is fine here because tmp is local
                                    latest_frame_ = tmp.clone();
                                    has_new_frame_flg_ = true;
                                }
                            }
                        }
                    }
                    av_packet_unref(packet);
                }
            }
        }
        catch (std::runtime_error &ex){
            std::cerr<<"Runtime error in decode_loop:"<< ex.what() << std::endl;
        }
        catch (...){
            std::cerr<<"Unhandled error in decode_loop"<<std::endl;
        }

        if (buffer) av_free(buffer);
        if (pFrameBGR) av_frame_free(&pFrameBGR);
        running_flg_ = false;
        av_frame_free(&pFrame);
        av_packet_free(&packet);
        sws_freeContext(sws_ctx);

    } // End of decode_loop

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
        
        /*******************************
        * Performance improvement flags
        *******************************/

        // Disable frame reordering (removes the delay caused by B-frames)
        // pCodecCtx->has_b_frames = 0; 

        // Low Latency Decoder Flags
        pCodecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

        // Explicitly skips decoding any frame that is not used as a reference point.
        // This instantly drops the work your CPU has to do from 60fps to ~30fps.
        // pCodecCtx->skip_frame = AVDISCARD_NONREF;

        // Can drop more frame if needed
        // pCodecCtx->skip_frame = AVDISC_CARD_BIDIR; 

        // For H.264, use one thread to avoid frame-threading latency (optional)
        pCodecCtx->thread_count = 1; 
        // pCodecCtx->thread_count = 0; 
        // pCodecCtx->thread_type = FF_THREAD_SLICE; 
        /******************************/

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

    /************ Thread safe of starting render ***************/
    void start_render_loop(std::string config_file, int stream_id=1, bool display=false) {
        // Wait for the first track come
        {
            std::unique_lock lock(mutex_);
            if (sws_ctx==nullptr) {
                std::cout<<"Wait for getting track\n";
                // cv_.wait(lock, [this]() { return !tracks_to_render_.empty(); });
                // Set timeout 7 seconds for waiting 
                if(!cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return sws_ctx!=nullptr; }))
                {
                    throw std::runtime_error("start_render_loop timeout after 7 seconds");
                }
                std::cout<<"Get track success\n";
            }
        }

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

            if (ready && render && running_flg_) {
                ready = false;
                render->on_frame(frame_to_process);
            }

            videostream::VideoRenderer::run_iteration(render);
        }
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


    try{
        // Set the credentials and enable the stats
        json streams_config = config_["source"]["entries"];

        std::string url = "";
        // json streams_config;
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
            return -1;
        }
        decoder.start_render_loop(args.config_file, args.stream_id, args.display);
    }
    catch (...) {
        std::cerr<<"Unhandled exception in play stream"<<std::endl;
    }

    return 0;
}
