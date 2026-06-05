#include "audio_device_selector.hpp"
#include <sstream>
#include <cstring>

#ifdef __APPLE__

std::vector<AudioDevice> AudioDeviceSelector::get_output_devices_macos() {
    std::vector<AudioDevice> devices;
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };
    
    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        nullptr,
        &dataSize
    );
    
    if (status != noErr) {
        std::cerr << "Error getting device list size: " << status << std::endl;
        return devices;
    }
    
    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> deviceIDs(deviceCount);
    
    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        nullptr,
        &dataSize,
        deviceIDs.data()
    );
    
    if (status != noErr) {
        std::cerr << "Error getting device list: " << status << std::endl;
        return devices;
    }
    
    // Filter for output devices only
    for (AudioDeviceID deviceID : deviceIDs) {
        AudioObjectPropertyAddress outputAddress = {
            kAudioDevicePropertyStreams,
            kAudioDevicePropertyScopeOutput,
            kAudioObjectPropertyElementMaster
        };
        
        dataSize = 0;
        status = AudioObjectGetPropertyDataSize(deviceID, &outputAddress, 0, nullptr, &dataSize);
        
        if (status == noErr && dataSize > 0) {
            // Get device name
            char deviceName[256];
            UInt32 nameSize = sizeof(deviceName);
            AudioObjectPropertyAddress nameAddress = {
                kAudioDevicePropertyDeviceName,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMaster
            };
            
            status = AudioObjectGetPropertyData(
                deviceID,
                &nameAddress,
                0,
                nullptr,
                &nameSize,
                deviceName
            );
            
            if (status == noErr) {
                AudioDevice device;
                device.id = std::to_string(deviceID);
                device.name = std::string(deviceName);
                devices.push_back(device);
            }
        }
    }
    
    return devices;
}

bool AudioDeviceSelector::set_output_device_macos(const std::string& device_id) {
    try {
        AudioDeviceID deviceID = static_cast<AudioDeviceID>(std::stoul(device_id));
        
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        
        OSStatus status = AudioObjectSetPropertyData(
            kAudioObjectSystemObject,
            &propertyAddress,
            0,
            nullptr,
            sizeof(AudioDeviceID),
            &deviceID
        );
        
        if (status == noErr) {
            std::cout << "Successfully set output device to: " << device_id << std::endl;
            return true;
        } else {
            std::cerr << "Error setting output device: " << status << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Invalid device ID: " << e.what() << std::endl;
        return false;
    }
}

#elif __linux__

static pa_context *pa_ctx = nullptr;
static pa_mainloop *pa_ml = nullptr;

static void context_state_callback(pa_context *c, void *userdata) {
    pa_context_state_t state = pa_context_get_state(c);
    if (state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        pa_mainloop_quit(pa_ml, 0);
    }
}

std::vector<AudioDevice> AudioDeviceSelector::get_output_devices_linux() {
    std::vector<AudioDevice> devices;
    
    pa_mainloop *ml = pa_mainloop_new();
    pa_mainloop_api *mlapi = pa_mainloop_get_api(ml);
    pa_context *c = pa_context_new(mlapi, nullptr);
    
    pa_context_set_state_callback(c, context_state_callback, nullptr);
    
    if (pa_context_connect(c, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0) {
        std::cerr << "pa_context_connect() failed" << std::endl;
        pa_context_unref(c);
        pa_mainloop_free(ml);
        return devices;
    }
    
    pa_ml = ml;
    pa_ctx = c;
    
    // Run until connected
    int ret = 0;
    pa_mainloop_run(ml, &ret);
    
    // Get sink list (output devices)
    pa_operation *op = pa_context_get_sink_info_list(c, 
        [](pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
            if (eol) {
                pa_mainloop_quit(pa_ml, 0);
                return;
            }
            
            if (i) {
                auto& devices = *static_cast<std::vector<AudioDevice>*>(userdata);
                AudioDevice device;
                device.id = i->name;
                device.name = i->description;
                devices.push_back(device);
            }
        }, 
        &devices
    );
    
    if (op) {
        ret = 0;
        pa_mainloop_run(ml, &ret);
        pa_operation_unref(op);
    }
    
    pa_context_disconnect(c);
    pa_context_unref(c);
    pa_mainloop_free(ml);
    
    return devices;
}

bool AudioDeviceSelector::set_output_device_linux(const std::string& device_id) {
    // On Linux with PulseAudio, we set the environment variable for the Millicast SDK
    if (setenv("PULSE_SINK", device_id.c_str(), 1) == 0) {
        std::cout << "Set PULSE_SINK to: " << device_id << std::endl;
        std::cout << "Note: Restart audio playback for the change to take effect" << std::endl;
        return true;
    } else {
        std::cerr << "Failed to set PULSE_SINK environment variable" << std::endl;
        return false;
    }
}

#endif

std::vector<AudioDevice> AudioDeviceSelector::get_output_devices() {
#ifdef __APPLE__
    return get_output_devices_macos();
#elif __linux__
    return get_output_devices_linux();
#else
    std::cerr << "Unsupported platform for audio device selection" << std::endl;
    return std::vector<AudioDevice>();
#endif
}

bool AudioDeviceSelector::set_output_device(const std::string& device_id) {
#ifdef __APPLE__
    return set_output_device_macos(device_id);
#elif __linux__
    return set_output_device_linux(device_id);
#else
    std::cerr << "Unsupported platform for audio device selection" << std::endl;
    return false;
#endif
}

std::string AudioDeviceSelector::get_default_output_device() {
    auto devices = get_output_devices();
    if (!devices.empty()) {
        return devices[0].name;
    }
    return "Unknown";
}

void AudioDeviceSelector::print_devices(const std::vector<AudioDevice>& devices) {
    if (devices.empty()) {
        std::cout << "No output devices found" << std::endl;
        return;
    }
    
    std::cout << "\n=== Available Audio Output Devices ===" << std::endl;
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << i << "] " << devices[i].name << " (ID: " << devices[i].id << ")" << std::endl;
    }
    std::cout << "======================================\n" << std::endl;
}
