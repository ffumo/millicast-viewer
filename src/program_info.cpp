
#include "build_info.h"
#include "argparse.hpp"
#include "program_info.hpp"


ProgramInfo::ProgramInfo(std::string name): name(name), 
    config_file(_config_file), display(_display), 
    disable_stats(_disable_stats), stream_id(_stream_id), 
    debug(_debug), sampling_mode(_sampling_mode), audio(_audio),
    audio_device(_audio_device) {
    
    version = build_info::version +
                            "\nSDK version: " + build_info::millicast_sdk_version + 
                            "\nFFmpeg version: " + build_info::ffmpeg_version + 
                            "\nOpenCV version: " + build_info::opencv_version + 
                            "\nBuild time " + build_info::build_date + " " + build_info::build_time;

    program_ptr = std::make_shared<argparse::ArgumentParser>(name, version);

    program_ptr->add_argument("-c", "--config")
        // .default_value(std::string{"configs/sample_config.json"})
        .default_value(std::string{""})
        // .required()
        .help("configuration file (required unless using --list-audio-devices)");

    program_ptr->add_argument("--id")
        .default_value(1)
        .help("stream id in configuration file")
        .scan<'i', int>();

    program_ptr->add_argument("-d", "--display")
        .help("enable display")
        .default_value(false)
        // .default_value(true)
        .implicit_value(true);

    program_ptr->add_argument("-q", "--quiet")
        .help("disable showing SDK stats")
        .default_value(false)
        // .default_value(true)
        .implicit_value(true);

    program_ptr->add_argument("-s", "--sample")
        .help("Run viewer in sampling mode")
        .default_value(false)
        // .default_value(true)
        .implicit_value(true);

    program_ptr->add_argument("--debug")
        .help("enable debug logs")
        .default_value(false)
        // .default_value(true)
        .implicit_value(true);

    program_ptr->add_argument("-a", "--audio")
        .help("enable audio")
        .default_value(false)
        // .default_value(true)
        .implicit_value(true);

    program_ptr->add_argument("--audio-device")
        .help("audio output device ID or name (e.g., 'Blackhole' on macOS or 'alsa_output.pci...' on Linux). Use --list-audio-devices to see available devices")
        .default_value(std::string{""})
        .nargs(1);

    program_ptr->add_argument("--list-audio-devices")
        .help("list available audio output devices and exit")
        .default_value(false)
        .implicit_value(true);
}

void ProgramInfo::parse_arguments(int argc, char* argv[]){
    try {
        program_ptr->parse_args(argc, argv);
        _config_file = program_ptr->get<std::string>("--config");  // "orange"
        _display = program_ptr->get<bool>("--display");
        _stream_id = program_ptr->get<int>("--id");
        _disable_stats = program_ptr->get<bool>("--quiet");
        _debug = program_ptr->get<bool>("--debug");
        _audio = program_ptr->get<bool>("--audio");
        _sampling_mode = program_ptr->get<bool>("--sample");
        _audio_device = program_ptr->get<std::string>("--audio-device");
        // _list_audio_device = program_ptr->get<bool>("--list-audio-devices");

        // Skip config file loading if only listing audio devices
        if (program_ptr->is_used("--list-audio-devices")) {
            return;
        }
        
        if (config_file.empty() || !config_file.length()) {
            std::cerr << "Error: --config is required" << std::endl;
            throw std::runtime_error("--config is required, but missing");
        }
    }
    catch (const std::exception& err) {
        std::cerr << "Error in parser argument: " << err.what() << std::endl;
        std::cerr << program_ptr.get();
        std::exit(EXIT_FAILURE);
    }
}

void ProgramInfo::print_args() {
    std::cout << "Version: "<< version << std::endl;
    std::cout << "config_file: "<< config_file << std::endl;
    std::cout << "display: "<< display << std::endl;
    std::cout << "stream_id: "<< stream_id << std::endl;
    std::cout << "disable_stats: "<< disable_stats << std::endl;
    std::cout << "debug: "<< debug << std::endl;
    std::cout << "audio: "<< audio << std::endl;
    std::cout << "sampling_mode: "<< sampling_mode << std::endl;
    std::cout << "audio_device: "<< (audio_device.empty() ? "default" : audio_device) << std::endl;
}

bool ProgramInfo::is_argument_used(const std::string& arg_name) const {
    return program_ptr->is_used(arg_name);
}
