#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <rawreader.h>
#include <core.h>
#include <gui/style.h>
#include <gui/widgets/file_select.h>
#include <filesystem>
#include <regex>
#include <gui/tuner.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "raw_file_source",
    /* Description:     */ "raw file source module for SDR++",
    /* Author:          */ "Ryzerth;theverygaming",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class RawFileSourceModule : public ModuleManager::Instance {
public:
    RawFileSourceModule(std::string name) : fileSelect("", { "All Files", "*" }) {
        this->name = name;

        if (core::args["server"].b()) { return; }

        config.acquire();
        fileSelect.setPath(config.conf["path"], true);
        config.release();

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;
        sigpath::sourceManager.registerSource("rawFile", &handler);
    }

    ~RawFileSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("rawFile");
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

private:
    static void menuSelected(void* ctx) {
        RawFileSourceModule* _this = (RawFileSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->centerFreq);
        sigpath::iqFrontEnd.setBuffering(false);
        gui::waterfall.centerFrequencyLocked = true;
        // gui::freqSelect.minFreq = _this->centerFreq - (_this->sampleRate/2);
        // gui::freqSelect.maxFreq = _this->centerFreq + (_this->sampleRate/2);
        // gui::freqSelect.limitFreq = true;
        flog::info("RawFileSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        RawFileSourceModule* _this = (RawFileSourceModule*)ctx;
        sigpath::iqFrontEnd.setBuffering(true);
        // gui::freqSelect.limitFreq = false;
        gui::waterfall.centerFrequencyLocked = false;
        flog::info("RawFileSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        RawFileSourceModule* _this = (RawFileSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        if (_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->running = true;
        _this->workerThread = std::thread(worker, _this);
        flog::info("RawFileSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        RawFileSourceModule* _this = (RawFileSourceModule*)ctx;
        if (!_this->running) { return; }
        if (_this->reader == NULL) { return; }
        _this->stream.stopWriter();
        _this->workerThread.join();
        _this->stream.clearWriteStop();
        _this->running = false;
        _this->reader->rewind();
        flog::info("RawFileSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        RawFileSourceModule* _this = (RawFileSourceModule*)ctx;
        flog::info("RawFileSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        RawFileSourceModule* _this = (RawFileSourceModule*)ctx;

        if (_this->fileSelect.render("##raw_file_source_" + _this->name)) {
            if (_this->fileSelect.pathIsValid()) {
                if (_this->reader != NULL) {
                    _this->reader->close();
                    delete _this->reader;
                }
                try {
                    _this->reader = new rawReader(_this->fileSelect.path, _this->rawFormat_int[_this->currentModeItem], _this->currentChannelsItem + 1);
                    core::setInputSampleRate(_this->sampleRate);
                    std::string filename = std::filesystem::path(_this->fileSelect.path).filename().string();
                    _this->centerFreq = _this->getFrequency(filename);
                    tuner::tune(tuner::TUNER_MODE_IQ_ONLY, "", _this->centerFreq);
                    // gui::freqSelect.minFreq = _this->centerFreq - (_this->sampleRate/2);
                    // gui::freqSelect.maxFreq = _this->centerFreq + (_this->sampleRate/2);
                    // gui::freqSelect.limitFreq = true;
                }
                catch (std::exception e) {
                    flog::error("Error: {0}", e.what());
                }
                config.acquire();
                config.conf["path"] = _this->fileSelect.path;
                config.release(true);
            }
        }

        if (_this->running) { style::beginDisabled(); }

        ImGui::InputDouble("samplerate##_raw_file_source", &_this->sampleRate, 1000, 1000000, "%.0f");
        if (ImGui::Combo("channel(s)##_raw_file_source", &_this->currentChannelsItem, "1\0\x32\0")) {
            _this->reader->setChannelCount(_this->currentChannelsItem + 1);
            
        }
        if (ImGui::Combo("type##_raw_file_source", &_this->currentModeItem, "u8\0s8\0s16\0f32\0")) {
            _this->reader->setFormat(_this->rawFormat_int[_this->currentModeItem]);
        }

        if (ImGui::Checkbox("WAV Mode(skip header)##_raw_file_source", &_this->wavMode)) {
            _this->reader->setWAV(_this->wavMode);
        }

        if (_this->running) { style::endDisabled(); }
    }

    static void worker(void* ctx) {
        RawFileSourceModule* _this = (RawFileSourceModule*)ctx;
        int blockSize = _this->sampleRate / 200.0f;
        dsp::complex_t* inBuf = new dsp::complex_t[blockSize];

        while (true) {
            _this->reader->readSamples(_this->stream.writeBuf, blockSize);
            if (!_this->stream.swap(blockSize)) { break; };
        }

        delete[] inBuf;
    }

    double getFrequency(std::string filename) {
        std::regex expr("[0-9]+Hz");
        std::smatch matches;
        std::regex_search(filename, matches, expr);
        if (matches.empty()) { return 0; }
        std::string freqStr = matches[0].str();
        return std::atof(freqStr.substr(0, freqStr.size() - 2).c_str());
    }

    FileSelect fileSelect;
    std::string name;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    rawReader* reader = NULL;
    bool running = false;
    bool enabled = true;
    double sampleRate = 4000000;
    std::thread workerThread;

    double centerFreq = 100000000;

    int currentChannelsItem = 1;
    int currentModeItem = 2;
    bool wavMode = false;

    std::map<int, rawReader::rawFormat> rawFormat_int{ { 0, rawReader::rawFormat::u8 }, { 1, rawReader::rawFormat::s8 }, { 2, rawReader::rawFormat::s16 }, { 3, rawReader::rawFormat::f32 } };
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["path"] = "";
    config.setPath(core::args["root"].s() + "/raw_file_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new RawFileSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (RawFileSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
