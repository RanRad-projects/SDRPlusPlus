#include <config.h>
#include <core.h>
#include <filesystem>
#include <mutex>
#include <regex>
#include <iostream>
#include <fstream>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/widgets/image.h>
#include <gui/widgets/folder_select.h>
#include <imgui.h>
#include <module.h>
#include <signal_path/signal_path.h>

#include <dsp/demod/quadrature.h>
#include <dsp/sink/handler_sink.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

class tv_rx {
public:
    tv_rx(int width, int height, float sync_lvl, float min, float span, bool liveview, bool autosync) {
        _width = width;
        _height = height;

        set_parameters(sync_lvl, min, span, liveview, autosync);

        hsync_buf = new uint32_t[width];
        vsync_buf = new uint32_t[height];
        img_buf = new uint8_t[_width * _height * 4];
        img_obuf = new uint8_t[_width * _height * 4];
    }

    void set_parameters(float sync_lvl, float min, float span, bool liveview, bool autosync) {
        _sync_lvl = sync_lvl;
        _min = min;
        _span = span;
        live_view = liveview;
        auto_sync = autosync;
    }

    ~tv_rx() {
        delete[] hsync_buf;
        delete[] vsync_buf;
        delete[] img_buf;
        delete[] img_obuf;
    }

    void input(float* data, size_t count) {
        if (skipsamples != 0) {
            if (skipsamples <= count) {
                count -= skipsamples;
                skipsamples = 0;
            }
            else {
                skipsamples -= count;
                count = 0;
            }
        }

        float val;
        float avg;
        float imval;
        int x_shifted_pos = 0;
        int pos = 0;
        for (int i = 0; i < count; i++) {
            val = data[i];

            // Draw
            imval = std::clamp<float>((val - _min) * 255.0 / _span, 0, 255);

            pos = ((_width * ypos) + xpos) * 4;

            img_buf[pos] = imval;
            img_buf[pos + 1] = imval;
            img_buf[pos + 2] = imval;
            img_buf[pos + 3] = 255;

            if (val < _sync_lvl) {
                if (live_view) {
                    // mark sync pixels
                    img_buf[pos] = 255;
                    img_buf[pos + 1] = 0;
                    img_buf[pos + 2] = 0;
                    img_buf[pos + 3] = 255;
                }
                hsync_buf[xpos]++;
                vsync_buf[ypos]++;
            }

            // Image logic
            xpos++;
            if (xpos >= _width) {
                ypos++;
                xpos = 0;
            }
            if (ypos >= _height) {
                ypos = 0;
                xpos = 0;

                int xoffset = std::distance(hsync_buf, std::max_element(hsync_buf, hsync_buf + _width));
                int yoffset = std::distance(vsync_buf, std::max_element(vsync_buf, vsync_buf + _height));
                memset(hsync_buf, 0, sizeof(*hsync_buf) * _width);
                memset(vsync_buf, 0, sizeof(*vsync_buf) * _height);

                int offset = xoffset + (yoffset * _width);

                if(auto_sync) {
                    if(yoffset > 10) { // we don't want to skip too much by accident
                        skipsamples = offset;
                    }
                }

                memcpy(img_obuf, img_buf + (offset * 4), ((_width * _height) - offset) * 4);
                memcpy(img_obuf + (((_width * _height) - offset) * 4), img_buf, offset * 4);

                image_done = true;
            }
        }
    }

    bool draw(uint8_t* rgba) {
        if (live_view) {
            memcpy(rgba, img_buf, _width * _height * 4);
            return true;
        }
        if (!image_done) {
            return false;
        }
        image_done = false;
        memcpy(rgba, img_obuf, _width * _height * 4);
        return true;
    }


private:
    int _width;
    int _height;

    float _sync_lvl;
    float _min;
    float _span;

    uint32_t* hsync_buf;
    uint32_t* vsync_buf;

    uint8_t* img_buf;
    uint8_t* img_obuf;

    bool live_view = false;
    bool auto_sync = false;

    // used by decoder directly
    int xpos = 0;
    int ypos = 0;
    size_t skipsamples = 0;
    bool image_done = false;
};

SDRPP_MOD_INFO{ /* Name:            */ "satv_decoder",
                /* Description:     */ "SATV decoder for SDR++",
                /* Author:          */ "theverygaming",
                /* Version:         */ 0, 1, 0,
                /* Max instances    */ -1 };

#define MAX_SR 500000
#define MIN_SR 1000

class SATVDecoderModule : public ModuleManager::Instance {
public:
    SATVDecoderModule(std::string name) : folderSelect("%ROOT%/recordings") {
        this->name = name;

        root = (std::string)core::args["root"];

        img = new ImGui::ImageDisplay(width, height);

        tv_dec = new tv_rx(width, height, sync_level, minLvl, spanLvl, live_view, auto_sync);

        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, samplerate, samplerate, MIN_SR, samplerate, false);
        
        imgbuf = new uint8_t[width * height];

        demod.init(vfo->output, MAX_SR, MAX_SR / 2.0f);
        sink.init(&demod.out, handler, this);

        demod.start();
        sink.start();

        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~SATVDecoderModule() {
        delete img;
        delete tv_dec;
        delete[] imgbuf;
        if (vfo) {
            sigpath::vfoManager.deleteVFO(vfo);
        }
        demod.stop();
        gui::menu.removeEntry(name);
    }

    void postInit() {}

    void enable() {
        start();
        enabled = true;
    }

    void disable() {
        enabled = false;
        stop();
    }

    bool isEnabled() { return enabled; }

private:
    void start() {
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, samplerate, samplerate, MIN_SR, samplerate, false);
        demod.setInput(vfo->output);

        demod.start();
        sink.start();
    }

    void stop() {
        demod.stop();
        sink.stop();

        sigpath::vfoManager.deleteVFO(vfo);
    }

    static void menuHandler(void* ctx) {
        SATVDecoderModule* _this = (SATVDecoderModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (!_this->enabled) {
            style::beginDisabled();
        }

        ImGui::FillWidth();
        _this->img->draw();

        ImGui::Checkbox("Lock settings", &_this->locked_settings);

        if (_this->locked_settings && _this->enabled) {
            style::beginDisabled();
        }

        _this->folderSelect.render("##folderselect");

        if (!_this->recording) {
            if (ImGui::Button("Record", ImVec2(menuWidth, 0))) {
                if (_this->folderSelect.pathIsValid()) {
                    _this->outfile_mtx.lock();
                    if(_this->outfile.is_open()) {
                        _this->outfile.close();
                        _this->outfile.clear();
                    }
                    _this->outfile.open(_this->expandString(_this->folderSelect.path + "/" + genFileName("$t_$f_$h-$m-$s_$d-$M-$y", "video", "video") + ".bin"), std::ios::binary);
                    if(_this->outfile.is_open()) {
                        _this->outfile << (uint64_t)_this->width;
                        _this->outfile << (uint64_t)_this->height;
                        _this->recording = true;
                    }
                    _this->outfile_mtx.unlock();
                }
            }
            ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "Idle");
        }
        else {
            if (ImGui::Button("Stop recording", ImVec2(menuWidth, 0))) {
                _this->outfile_mtx.lock();
                if(_this->outfile.is_open()) {
                    _this->outfile.close();
                    _this->outfile.clear();
                }
                _this->recording = false;
                _this->outfile_mtx.unlock();
            }
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Recording");
        }

        if (ImGui::Checkbox("Live view", &_this->live_view)) {
            _this->tv_dec->set_parameters(_this->sync_level, _this->minLvl, _this->spanLvl, _this->live_view, _this->auto_sync);
        }

        if (ImGui::Checkbox("Auto sync", &_this->auto_sync)) {
            _this->tv_dec->set_parameters(_this->sync_level, _this->minLvl, _this->spanLvl, _this->live_view, _this->auto_sync);
        }

        ImGui::LeftLabel("X shift");
        ImGui::FillWidth();
        ImGui::SliderInt("##xshift", &_this->x_shift, 0, _this->width);

        ImGui::LeftLabel("Sync");
        ImGui::FillWidth();

        if (ImGui::SliderFloat("##syncLvl", &_this->sync_level, -1, 1)) {
            _this->tv_dec->set_parameters(_this->sync_level, _this->minLvl, _this->spanLvl, _this->live_view, _this->auto_sync);
        }

        ImGui::LeftLabel("Min");
        ImGui::FillWidth();
        if (ImGui::SliderFloat("##minLvl", &_this->minLvl, -1.0, 1.0)) {
            _this->tv_dec->set_parameters(_this->sync_level, _this->minLvl, _this->spanLvl, _this->live_view, _this->auto_sync);
        }

        ImGui::LeftLabel("Span");
        ImGui::FillWidth();
        if (ImGui::SliderFloat("##spanLvl", &_this->spanLvl, 0, 1.0)) {
            _this->tv_dec->set_parameters(_this->sync_level, _this->minLvl, _this->spanLvl, _this->live_view, _this->auto_sync);
        }

        if (_this->locked_settings && _this->enabled) {
            style::endDisabled();
        }

        if (!_this->enabled) {
            style::endDisabled();
        }
    }

    static void handler(float* data, int count, void* ctx) {
        SATVDecoderModule* _this = (SATVDecoderModule*)ctx;
        uint8_t* buf = (uint8_t*)_this->img->activeBuffer;

        _this->tv_dec->input(data, count);

        if (_this->tv_dec->draw(buf)) {
            _this->img->update();
            if (_this->recording && !_this->live_view) {
                uint8_t *bufu8 = (uint8_t*)buf;
                for(size_t i = 0; i < _this->width * _this->height; i++) {
                    _this->imgbuf[i] = *bufu8;
                    bufu8 += 4; // RGBA
                }
                _this->outfile_mtx.lock();
                _this->outfile.write((char *)_this->imgbuf, _this->width * _this->height);
                _this->outfile_mtx.unlock();
            }
        }
    }

    static std::string genFileName(std::string templ, std::string type, std::string name) {
        // Get data
        time_t now = time(0);
        tm* ltm = localtime(&now);
        char buf[1024];
        double freq = gui::waterfall.getCenterFrequency();
        if (gui::waterfall.vfos.find(name) != gui::waterfall.vfos.end()) {
            freq += gui::waterfall.vfos[name]->generalOffset;
        }

        // Format to string
        char freqStr[128];
        char hourStr[128];
        char minStr[128];
        char secStr[128];
        char dayStr[128];
        char monStr[128];
        char yearStr[128];
        sprintf(freqStr, "%.0lfHz", freq);
        sprintf(hourStr, "%02d", ltm->tm_hour);
        sprintf(minStr, "%02d", ltm->tm_min);
        sprintf(secStr, "%02d", ltm->tm_sec);
        sprintf(dayStr, "%02d", ltm->tm_mday);
        sprintf(monStr, "%02d", ltm->tm_mon + 1);
        sprintf(yearStr, "%02d", ltm->tm_year + 1900);

        // Replace in template
        templ = std::regex_replace(templ, std::regex("\\$t"), type);
        templ = std::regex_replace(templ, std::regex("\\$f"), freqStr);
        templ = std::regex_replace(templ, std::regex("\\$h"), hourStr);
        templ = std::regex_replace(templ, std::regex("\\$m"), minStr);
        templ = std::regex_replace(templ, std::regex("\\$s"), secStr);
        templ = std::regex_replace(templ, std::regex("\\$d"), dayStr);
        templ = std::regex_replace(templ, std::regex("\\$M"), monStr);
        templ = std::regex_replace(templ, std::regex("\\$y"), yearStr);
        return templ;
    }

    std::string expandString(std::string input) {
        input = std::regex_replace(input, std::regex("%ROOT%"), root);
        return std::regex_replace(input, std::regex("//"), "/");
    }

    std::string name;
    bool enabled = true;

    VFOManager::VFO* vfo = NULL;
    dsp::demod::Quadrature demod;
    dsp::sink::Handler<float> sink;

    FolderSelect folderSelect;

    double samplerate = 100000;

    int width = 1285;
    int height = 730;

    int xpos = 0;
    int ypos = 0;

    int x_shift = 156;

    float sync_level = -0.058f;
    int sync_count = 0;

    float minLvl = -0.032f;
    float spanLvl = 0.110f;

    std::mutex outfile_mtx;
    std::ofstream outfile;

    bool locked_settings = true;
    bool live_view = false;
    bool auto_sync = true;

    bool recording = false;

    ImGui::ImageDisplay* img;

    tv_rx* tv_dec;

    uint8_t *imgbuf;

    std::string root;
};

MOD_EXPORT void _INIT_() {}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) { return new SATVDecoderModule(name); }

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) { delete (SATVDecoderModule*)instance; }

MOD_EXPORT void _END_() {}
