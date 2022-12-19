#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <core.h>
#include <signal_path/signal_path.h>
#include <signal_path/sink.h>
#include <spdlog/spdlog.h>
#include <RtAudio.h>
#include <config.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "audio_source",
    /* Description:     */ "Audio source module for SDR++",
    /* Author:          */ "theverygaming",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class AudioSourceModule : public ModuleManager::Instance {
public:
    AudioSourceModule(std::string name) {
        this->name = name;

        refresh();

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;
        sigpath::sourceManager.registerSource("Audio", &handler);
    }

    ~AudioSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("Audio");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void selectFirst() {
        selectById(defaultDevId);
    }

    void selectByName(std::string name) {
        for (int i =0; i < devList.size(); i++) {
            if (devList[i].name == name) {
                selectById(i);
                return;
            }
        }
        selectFirst();
    }

    void selectById(int id) {
        devId = id;
        bool created = false;
        config.acquire();
        if (!config.conf["devices"].contains(devList[id].name)) {
            created = true;
            config.conf["devices"][devList[id].name] = devList[id].preferredSampleRate;
        }
        sampleRate = config.conf["devices"][devList[id].name];
        config.release(created);

        sampleRates = devList[id].sampleRates;
        sampleRatesTxt = "";
        char buf[256];
        bool found = false;
        unsigned int defaultId = 0;
        unsigned int defaultSr = devList[id].preferredSampleRate;
        for (int i = 0; i < sampleRates.size(); i++) {
            if (sampleRates[i] == sampleRate) {
                found = true;
                srId = i;
            }
            if (sampleRates[i] == defaultSr) {
                defaultId = i;
            }
            sprintf(buf, "%d", sampleRates[i]);
            sampleRatesTxt += buf;
            sampleRatesTxt += '\0';
        }
        if (!found) {
            sampleRate = defaultSr;
            srId = defaultId;
        }

        core::setInputSampleRate(sampleRate);

        if (running) {
            doStop();
            doStart();
        }
    }

private:
    static void menuSelected(void* ctx) {
        AudioSourceModule* _this = (AudioSourceModule*)ctx;

        std::string device = "";
        config.acquire();
        device = config.conf["device"];
        config.release(false);
        _this->selectByName(device);

        spdlog::info("AudioSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        AudioSourceModule* _this = (AudioSourceModule*)ctx;
        spdlog::info("AudioSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        AudioSourceModule* _this = (AudioSourceModule*)ctx;
        if (_this->running) {
            return;
        }
        _this->doStart();
        spdlog::info("AudioSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        AudioSourceModule* _this = (AudioSourceModule*)ctx;
        if (!_this->running) {
            return;
        }
        _this->doStop();
        spdlog::info("AudioSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        AudioSourceModule* _this = (AudioSourceModule*)ctx;
        spdlog::info("AudioSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        AudioSourceModule* _this = (AudioSourceModule*)ctx;

        float menuWidth = ImGui::GetContentRegionAvail().x;

        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo(("##_audio_source_dev_" + _this->name).c_str(), &_this->devId, _this->txtDevList.c_str())) {
            _this->selectById(_this->devId);
            config.acquire();
            config.conf["device"] = _this->devList[_this->devId].name;
            config.release(true);
        }

        ImGui::SetNextItemWidth(menuWidth);
        if (ImGui::Combo(("##_audio_source_sr_" + _this->name).c_str(), &_this->srId, _this->sampleRatesTxt.c_str())) {
            _this->sampleRate = _this->sampleRates[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            if (_this->running) {
                _this->doStop();
                _this->doStart();
            }
            config.acquire();
            config.conf["devices"][_this->devList[_this->devId].name] = _this->sampleRate;
            config.release(true);
        }
    }

    void refresh() {
        int count = audio.getDeviceCount();
        RtAudio::DeviceInfo info;
        txtDevList = "";
        devList.clear();
        deviceIds.clear();
        for (int i = 0; i < count; i++) {
            info = audio.getDeviceInfo(i);
            if (!info.probed) { continue; }
            if (info.inputChannels == 0) { continue; }
            if (info.isDefaultInput) { defaultDevId = devList.size(); }
            devList.push_back(info);
            deviceIds.push_back(i);
            txtDevList += info.name;
            txtDevList += '\0';
        }
    }

    void doStart() {
        RtAudio::StreamParameters parameters;
        parameters.deviceId = deviceIds[devId];
        parameters.nChannels = 2;
        unsigned int bufferFrames = sampleRate / 60;
        RtAudio::StreamOptions opts;
        opts.flags = RTAUDIO_MINIMIZE_LATENCY;

        try {
            audio.openStream(NULL, &parameters, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &callback, this, &opts);
            audio.startStream();
        } catch (RtAudioError& e) {
            spdlog::error("Could not open audio device");
            return;
        }

        running = true;
        spdlog::info("RtAudio stream open");
    }

    void doStop() {
        audio.stopStream();
        audio.closeStream();
        stream.stopWriter();
        stream.clearWriteStop();
        running = false;
    }

    static int callback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames, double streamTime, RtAudioStreamStatus status, void* userData) {
        AudioSourceModule* _this = (AudioSourceModule*)userData;
        memcpy(_this->stream.writeBuf, inputBuffer, nBufferFrames*sizeof(dsp::complex_t));
        _this->stream.swap(nBufferFrames);
        return 0;
    }

    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;

    int srId = 0;
    int devCount;
    int devId = 0;
    bool running = false;

    unsigned int defaultDevId = 0;

    std::vector<RtAudio::DeviceInfo> devList;
    std::vector<unsigned int> deviceIds;
    std::string txtDevList;

    std::vector<unsigned int> sampleRates;
    std::string sampleRatesTxt;
    unsigned int sampleRate = 48000;
    std::string name;
    bool enabled = true;

    RtAudio audio;
};

MOD_EXPORT void _INIT_() {
    json def;
    def["device"] = "";
    def["devices"] = json({});
    config.setPath(core::args["root"].s() + "/audio_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new AudioSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (AudioSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}