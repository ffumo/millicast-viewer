
#include <memory>
#include "argparse.hpp"
#include "vector"

class ProgramInfo {
private:
    void print_build_info();
    std::string name="Viewer";
    std::string version="";
    std::shared_ptr<argparse::ArgumentParser> program_ptr;
    std::string _config_file;
    int _stream_id;
    bool _display;
    bool _debug;
    bool _audio;
    bool _disable_stats;
    bool _sampling_mode;
    // bool _list_audio_device;
    std::string _audio_device;
    
public:
    ProgramInfo(std::string name);
    ~ProgramInfo(){};
    void print_args();
    void parse_arguments(int argc, char* argv[]);
    bool is_argument_used(const std::string& arg_name) const;
    const std::string & config_file;
    const int& stream_id;
    const bool& display;
    const bool& debug;
    const bool& audio;
    const bool& sampling_mode;
    const bool& disable_stats;
    // const bool& list_audio_device;
    const std::string& audio_device;
};
