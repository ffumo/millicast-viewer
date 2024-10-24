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
#include <stdio.h>

#include <millicast-sdk/mc_logging.h>
#include <millicast-sdk/renderer.h>
#include <millicast-sdk/stats.h>
#include <millicast-sdk/track.h>
#include <millicast-sdk/viewer.h>

#include "utils.h"

#include "nlohmann/json.hpp"

#include <opencv2/opencv.hpp>

// #ifdef WITH_CV_RENDERER
#include "argparse.hpp"
#include "cv_renderer.h"
// #endif


using json = nlohmann::json;


std::string get_env(const char* var) {
    const char* ret = std::getenv(var);
    return (ret) ? ret : "";
}

millicast::Viewer::Credentials get_stream_credentials(std::string acc_id, std::string stream, std::string token="") {
    millicast::Viewer::Credentials credentials;
    // credentials.stream_name = get_env("TEST_STREAM_NAME");  // stream_name
    credentials.stream_name = stream;
    // credentials.account_id = get_env("TEST_ACCOUNT_ID");    // The ID of your Dolby.io Real-time Streaming account
    credentials.account_id = acc_id;
    credentials.api_url = "https://director.millicast.com/api/director/subscribe";  // subscribe_url
    if (token != "") 
    {
        credentials.token = token;
    }

    if (credentials.stream_name.length() == 0 ||
        credentials.account_id.length() == 0) {
        throw std::runtime_error(
            "Invalid credentials for publishing. Values must be non-empty.");
    }
    return credentials;
}

class TrackManager {

public:
    TrackManager() = default;
    void start_render_loop(std::string config_file, int stream_id=1, bool display=false) {
        // wait for the first track to come before running the render loop
        {
            std::unique_lock lock(mutex_);
            if (tracks_to_render_.empty()) {
                cv_.wait(lock, [this]() { return !tracks_to_render_.empty(); });
            }
        }
        do {
            std::lock_guard lock(mutex_);
            if (!tracks_to_render_.empty()) {
                auto* track = tracks_to_render_.front();
                tracks_to_render_.pop_front();
                track->enable(add_renderer(config_file, stream_id, display));
                printf("Added render: %ld\n", renderers_.size());
            }
        } while (VideoRenderer::run_iteration(renderers_.back()));
    }

    const std::shared_ptr<VideoRenderer>& add_renderer(std::string config_file, int stream_id=1, bool display=false) {
        std::string render_name = "Track " + std::to_string((int)renderers_.size());
        printf("Create render: %s\n", render_name.c_str());
        std::shared_ptr<VideoRenderer> render = std::make_shared<VideoRenderer>(render_name);
        render->display_ = display;
        renderers_.push_back(render);
        renderers_.back()->init(config_file, stream_id);
        printf("Created render: %s\n", render->title_.c_str());
        return renderers_.back();
    }

    ~TrackManager() {
        for (auto& handler : handlers_) {
            handler->disconnect();
        }
    }

    void add_video_track(millicast::RtsRemoteVideoTrack* track) {
        handlers_.push_back(track->add_event_handler(
            [id{track->source_id().value_or("null")}](
                const millicast::RtsRemoteVideoTrack::Active& evt) {
                    millicast::Logger::log("Track: " + id + "is now active!", millicast::LogLevel::MC_LOG);
            }));

        handlers_.push_back(track->add_event_handler(
            [id{track->source_id().value_or("null")}](
                const millicast::RtsRemoteVideoTrack::Inactive& evt) {
                    millicast::Logger::log("Track: " + id + "is now inactive!", millicast::LogLevel::MC_LOG);
            }));

        handlers_.push_back(track->add_event_handler(
            [id{track->source_id().value_or("null")}](
                const millicast::RtsRemoteVideoTrack::Layers& evt) {
                    millicast::Logger::log("Track: " + id + "layers event! Layers:", millicast::LogLevel::MC_LOG);
                for (const auto& layer : evt.active) {
                    int width = 0, height = 0;
                    if (layer.layer_resolution.has_value()) {
                        width = layer.layer_resolution->width;
                        height = layer.layer_resolution->height;
                    }
                    millicast::Logger::log(
                        "Layer " + std::to_string(width) + "x" +
                            std::to_string(height) +
                            "p EncodingId: " + layer.encoding_id + " TemporalLayerId:" +
                            std::to_string(layer.temporal_layer_id.value_or(-1)) +
                            " SpatialLayerID:" +
                            std::to_string(layer.spatial_layer_id.value_or(-1)) +
                            " Target Bitrate: " +
                            std::to_string(layer.target_bitrate.value_or(-1)) +
                            " Target Resolution: " +
                            std::to_string(layer.target_width.value_or(-1)) + "x" +
                            std::to_string(layer.target_height.value_or(-1)),
                        millicast::LogLevel::MC_LOG);
                }
            }));

        handlers_.push_back(
            track->add_event_handler(
                [id{track->source_id().value_or("null")}](const millicast::FrameMetadata& evt) {
                    assert(evt.sei_messages.size() > 0);
                    for (const auto& sei_message : evt.sei_messages) {
                        if (sei_message->type() == millicast::SeiMessageType::UNREGISTERED_USER_DATA) {
                            millicast::SeiUserUnregisteredData* unregistered = sei_message->sei_user_unregistered_message();
                            if (unregistered->user_data().empty()) {
                                millicast::Logger::log("Received empty user data!", millicast::LogLevel::MC_WARNING);
                                return;
                            }
                            auto x = *reinterpret_cast<const double*>(unregistered->user_data().data());
                            auto y = *reinterpret_cast<const double*>(unregistered->user_data().data() + METADATA_APPEND_INDEX);
                            std::stringstream ss;
                            ss << "Track: " << id << " metadata: x = " << x << " y = " << y;
                            millicast::Logger::log(ss.str(), millicast::LogLevel::MC_DEBUG);
                        }
                    }
                }));

        handlers_.push_back(track->add_event_handler(
            [id{track->source_id().value_or("null")}](
                const millicast::RtsRemoteVideoTrack::MidUpdated& evt) {
                    millicast::Logger::log("Track: " + id + " mid updated!",
                    millicast::LogLevel::MC_LOG);
            }));

        tracks_.push_back(track);

        // Add this track for processing:
        {
            std::lock_guard lock(mutex_);
            tracks_to_render_.push_back(track);
        }
        cv_.notify_one();
    }

    void add_audio_track(millicast::RtsRemoteAudioTrack* track) {
        audio_tracks_.push_back(std::move(track));
    }

private:
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<millicast::RtsRemoteVideoTrack*> tracks_to_render_;
    std::vector<millicast::RtsRemoteVideoTrack*> tracks_;
    std::vector<millicast::RtsRemoteAudioTrack*> audio_tracks_;
    std::vector<millicast::EventConnectionPtr> handlers_;
    std::vector<std::shared_ptr<VideoRenderer>> renderers_;
};

void setup_viewer_event_handlers(millicast::Viewer* viewer,
std::vector<millicast::EventConnectionPtr>& handlers,
TrackManager& track_manager) {
    handlers.push_back(
        viewer->add_event_handler([](const millicast::Client::StatsEvent& evt) {
            using namespace millicast::rtcstats;
            std::ostringstream oss;
            Utils utils;
            oss << "[Stat] " << utils.get_stats_str<InboundRtpStream>(*evt.report)
                << "\n";
            millicast::Logger::log(oss.str(), millicast::LogLevel::MC_LOG);
        }));
    handlers.push_back(
        viewer->add_event_handler([](const millicast::Client::ViewerCount& evt) {
            millicast::Logger::log(
                "Viewer Count updated: " + std::to_string(evt.viewer_count),
                millicast::LogLevel::MC_LOG);
        }));
    handlers.push_back(viewer->add_event_handler(
        [](const millicast::Client::WebsocketState& evt) {
            millicast::Logger::log(
                "Websocket State Changed: " + Utils::translate(evt.state),
                millicast::LogLevel::MC_LOG);
        }));
    handlers.push_back(viewer->add_event_handler(
        [](const millicast::Client::PeerConnectionState& evt) {
            millicast::Logger::log(
                "PeerConnection State Change: " + Utils::translate(evt.state),
                millicast::LogLevel::MC_LOG);
        }));
    handlers.push_back(viewer->add_event_handler(
        [](const millicast::Client::SignalingError& evt) {
            millicast::Logger::log("Signaling Error: " + evt.description,
                                    millicast::LogLevel::MC_WARNING);
        }));
    handlers.push_back(viewer->add_event_handler(
        [](const millicast::Client::HttpConnectionError& evt) {
            millicast::Logger::log("HttpConnection Error: " + evt.description +
                                    " code: " + std::to_string(evt.status_code),
                                    millicast::LogLevel::MC_WARNING);
        }));

    // This is the RtsTrackAdded event which is received when there is incoming
    // audio/video stream. This event is crucial for indicating to application
    // that there is something to be viewed. After which each track gets its own
    // events.
    handlers.push_back(
        viewer->add_event_handler(
            [&track_manager](const millicast::Viewer::RtsTrackAdded& evt) {
                millicast::Logger::log(
                    "RtsTrackAdded: " + evt.track.get_current_mid().value_or("no-mid"),
                    millicast::LogLevel::MC_LOG);
                if (auto* vid_track = evt.track.as_video(); vid_track != nullptr) {
                    // Uncomment this code out to enable frame metadata which is
                    // an experimental feature.
                    // vid_track->enable_frame_metadata(true);
                    track_manager.add_video_track(vid_track);
                } 
                else {
                    // auto at = evt.track.as_audio();
                    // at->enable().on_result([]() {
                    //     millicast::Logger::log("Rts Audio track enabled successfully",
                    //                         millicast::LogLevel::MC_LOG);
                    // });
                    // track_manager.add_audio_track(at);
                }
            }
        ));

    handlers.push_back(
        viewer->add_event_handler([](const millicast::Viewer::StreamStopped&) {
            millicast::Logger::log("Stream has stopped!",
                                millicast::LogLevel::MC_LOG);
        }));
}

void disconnect_event_handlers(
    std::vector<millicast::EventConnectionPtr>& handlers) {
    for (auto& handler : handlers) {
        handler->disconnect();
    }
    handlers.clear();
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    argparse::ArgumentParser program("Millicast viewer");

    program.add_argument("-c", "--config")
        // .default_value(std::string{"configs/sample_config.json"})
        .required()
        .help("configuration file");

    program.add_argument("--id")
        .default_value(1)
        .help("stream id in configuration file")
        .scan<'i', int>();

    program.add_argument("-d", "--display")
        .help("enable display")
        .default_value(false)
        // .default_value(true)
        .implicit_value(true);

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(EXIT_FAILURE);
    }
    auto config_file = program.get<std::string>("--config");  // "orange"
    auto display = program["--display"] == true;
    auto stream_id = program.get<int>("--id");

    std::cout << "config_file: "<< config_file << std::endl;
    std::cout << "display: "<< display << std::endl;
    std::cout << "stream_id: "<< stream_id << std::endl;

    nlohmann::json config_;
    try{
        std::ifstream f(config_file.c_str());
        // Global config
        nlohmann::json g_config = json::parse(f);
        // Copy stream config 1
        config_ = g_config["streams"]["stream_" + std::to_string(stream_id)];
    }
    catch(const std::ifstream::failure& e){
        std::cerr<<"Error in read configuration file: "<< config_file <<std::endl;
        exit(EXIT_FAILURE);
    }

    // std::exit(EXIT_SUCCESS);

    millicast::Logger::set_log_levels(millicast::LogLevel::MC_DEBUG,
                                      millicast::LogLevel::MC_ERROR,
                                      millicast::LogLevel::MC_ERROR);
    std::vector<millicast::Logger::LogComponent> components{
        millicast::Logger::LogComponent::SDK,
        millicast::Logger::LogComponent::WEBRTC
        };

    millicast::Logger::set_logger(
        [](const std::string& str, const std::string& comp,
            millicast::LogLevel level) {
            std::cerr << "[" << str << "|" << Utils::translate(level) << "] "
                        << comp << std::endl;
        },
        std::move(components));

    std::vector<millicast::EventConnectionPtr> viewer_handlers;
    try {
        // Creates the viewer, this is synchronous
        auto viewer = millicast::Viewer::create();

        // These are helpers for management of the incoming tracks of audio/video as
        // well as there event handlers/
        auto track_manager = std::make_unique<TrackManager>();
        setup_viewer_event_handlers(viewer.get(), viewer_handlers, *track_manager);

        // Set the credentials and enable the stats
        json stream_config = config_["source"]["entries"][0];
        std::string stream_token = "";
        if (stream_config.contains("token")) {
            stream_token = stream_config["token"];
        }
        std::cout<<"Get stream: "<< stream_config <<std::endl;
        wait(viewer->set_credentials(get_stream_credentials(stream_config["account_id"], stream_config["stream_name"], stream_token)));

        std::cout<<"Enable starts"<<std::endl;
        wait(viewer->enable_stats(true));

        // Connect to the millicast backend and then also subscribe to the stream
        millicast::ClientConnectionOptions opts{};
        opts.auto_reconnect = true;
        std::cout<<"Connect stream"<<std::endl;
        wait(viewer->connect(opts));
        std::cout<<"subscribe stream"<<std::endl;
        wait(viewer->subscribe());

        std::cout<<"Start render loop"<<std::endl;
        // Wait for any character to exit
        track_manager->start_render_loop(config_file, stream_id, display);

        // Unsubscribe and disconnect
        wait(viewer->unsubscribe());
        wait(viewer->disconnect());
    } catch (const std::error_condition& cond) {
        millicast::Logger::log("Error occurred " + cond.message(),
                              millicast::LogLevel::MC_ERROR);
    } 
    // catch (...) {
    //     millicast::Logger::log("Unknown error received!",
    //                           millicast::LogLevel::MC_ERROR);
    // }

    disconnect_event_handlers(viewer_handlers);
    return 0;
}
