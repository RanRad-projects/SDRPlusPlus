#include <imgui.h>
#include <implot.h>
#include <module.h>
#include <gui/gui.h>
#include <vector>
#include <cstring>
#include <thread>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "signalstrengthgraph",
    /* Description:     */ "Graph average signal strength",
    /* Author:          */ "theverygaming",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ -1
};

class signalstrengthgraph : public ModuleManager::Instance {
public:
    signalstrengthgraph(std::string name) {
        this->name = name;
        gui::menu.registerEntry(name, menuHandler, this, NULL);
        buffer = (float*)malloc(bufferSize * sizeof(float));
        memset(buffer, 0, bufferSize * sizeof(float));
    }

    ~signalstrengthgraph() {
        free(buffer);
        gui::menu.removeEntry(name);
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
    static void menuHandler(void* ctx) {
        signalstrengthgraph* _this = (signalstrengthgraph*)ctx;

        if (ImGui::Checkbox(CONCAT("enabled##_signalstrengthgraph_", _this->name), &_this->running)) {
            if (_this->running) {
                _this->start();
            }
            else {
                _this->stop();
            }
        }

        ImGui::Text("Note: values here will depend on zoom level!");
        ImGui::SliderFloat(CONCAT("% bw from center##_signalstrengthgraph_", _this->name), &_this->bandwidthPercent, 1, 100, "%.0f");
        ImGui::SliderInt(CONCAT("interval##_signalstrengthgraph_", _this->name), &_this->interval, 1, 300);

        ImGui::Text("max: %.2fdBFS level diff: %.2fdB", _this->maxValue, _this->maxValue - _this->level);

        if (ImGui::Button(CONCAT("reset##_signalstrengthgraph_", _this->name))) {
            _this->maxValue = -200;
        }
        ImGui::Text("level: %.2fdBFS", _this->level);
        if (ImGui::Button(CONCAT("level##_signalstrengthgraph_", _this->name))) {
            _this->level = getAveragePower(ctx);
        }

        if (_this->running) {
            ImGui::BeginDisabled();
        }

        if (_this->running) {
            ImGui::EndDisabled();
        }

        if (ImPlot::BeginPlot("Signal strength")) {
            ImPlot::PlotLine("dbFS", _this->buffer, _this->bufferSize);
            ImPlot::EndPlot();
        }
    }

    void start() {
        running = true;
        workerThread = std::thread(worker, this);
    }

    void stop() {
        running = false;
        workerThread.join();
    }

    static void worker(void* ctx) {
        signalstrengthgraph* _this = (signalstrengthgraph*)ctx;

        int counter = 0;

        while (_this->running) {
            float averagePower = getAveragePower(ctx);
            if (counter > (_this->bufferSize - 1)) {
                counter = 0;
            }
            _this->buffer[counter] = averagePower;
            if (averagePower > _this->maxValue) {
                _this->maxValue = averagePower;
            }
            counter++;
            std::this_thread::sleep_for(std::chrono::microseconds(_this->interval));
        }
    }

    static double getAveragePower(void* ctx) {
        signalstrengthgraph* _this = (signalstrengthgraph*)ctx;
        int fftSize;
        float* latestFFT = gui::waterfall.acquireLatestFFT(fftSize);

        int halfCount = ((float)fftSize * (_this->bandwidthPercent / 100)) / 2;
        int count = halfCount * 2;

        if (count == 0) {
            gui::waterfall.releaseLatestFFT();
            return -160;
        }

        double averageFFT = 0;
        for (int i = 0; i < count; i++) {
            int index = (((fftSize / 2)) - halfCount) + i;
            averageFFT += latestFFT[index];
        }

        gui::waterfall.releaseLatestFFT();

        averageFFT /= count;

        return averageFFT;
    }

    float bandwidthPercent = 100;
    int interval = 100;

    float maxValue = -200;
    float level = 0;

    float* buffer;
    size_t bufferSize = 100000;

    bool running = false;

    std::thread workerThread;

    std::string name;
    bool enabled = true;
};

MOD_EXPORT void _INIT_() {
    // Nothing here
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new signalstrengthgraph(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (signalstrengthgraph*)instance;
}

MOD_EXPORT void _END_() {
    // Nothing here
}
