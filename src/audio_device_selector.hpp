#ifndef __AUDIO_DEVICE_SELECTOR_HPP__
#define __AUDIO_DEVICE_SELECTOR_HPP__

#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>

#ifdef __APPLE__
    #include <CoreAudio/CoreAudio.h>
#elif __linux__
    #include <pulse/pulseaudio.h>
#endif

struct AudioDevice {
    std::string id;
    std::string name;
};

class AudioDeviceSelector {
public:
    static std::vector<AudioDevice> get_output_devices();
    static bool set_output_device(const std::string& device_id);
    static std::string get_default_output_device();
    static void print_devices(const std::vector<AudioDevice>& devices);

private:
#ifdef __APPLE__
    static std::vector<AudioDevice> get_output_devices_macos();
    static bool set_output_device_macos(const std::string& device_id);
#elif __linux__
    static std::vector<AudioDevice> get_output_devices_linux();
    static bool set_output_device_linux(const std::string& device_id);
#endif
};

#endif /* __AUDIO_DEVICE_SELECTOR_HPP__ */
