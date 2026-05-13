
#include "build_info.h"
#include "argparse.hpp"
#include "program_info.hpp"


ProgramInfo::ProgramInfo(std::string name): name(name), 
    config_file(_config_file), display(_display), disable_stats(_disable_stats), stream_id(_stream_id) {
    
    version = build_info::version +
                            "\nSDK version: " + build_info::millicast_sdk_version + 
                            "\nFFmpeg version: " + build_info::ffmpeg_version + 
                            "\nOpenCV version: " + build_info::opencv_version + 
                            "\nBuild time " + build_info::build_date + " " + build_info::build_time;

    program_ptr = std::make_shared<argparse::ArgumentParser>(name, version);

    program_ptr->add_argument("-c", "--config")
        // .default_value(std::string{"configs/sample_config.json"})
        .required()
        .help("configuration file");

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

}

void ProgramInfo::parse_arguments(int argc, char* argv[]){
    try {
        program_ptr->parse_args(argc, argv);
        _config_file = program_ptr->get<std::string>("--config");  // "orange"
        _display = program_ptr->get<bool>("--display");
        _stream_id = program_ptr->get<int>("--id");
        _disable_stats = program_ptr->get<bool>("--quiet");
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
}
