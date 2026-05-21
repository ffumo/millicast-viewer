
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
    bool _disable_stats;
    bool _sampling_mode;
    
public:
    ProgramInfo(std::string name);
    ~ProgramInfo(){};
    void print_args();
    void parse_arguments(int argc, char* argv[]);
    const std::string & config_file;
    const int& stream_id;
    const bool& display;
    const bool& debug;
    const bool& sampling_mode;
    const bool& disable_stats;
};
