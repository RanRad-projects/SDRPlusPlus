#pragma once
#include <stdint.h>
#include <string.h>
#include <dsp/types.h>
#include <map>
#include <string>
#include <fstream>
#include <volk/volk.h>

class rawReader {
public:
    enum class rawFormat { u8,
                           s8,
                           s16,
                           f32 };

    rawReader(std::string path, rawFormat format, int channelcount) {
        file = std::ifstream(path.c_str(), std::ios::binary);
        buffer = volk_malloc(1, volk_get_alignment());
        bufferSize = 1;
        setChannelCount(channelcount);
        setFormat(format);
    }

    ~rawReader() {
        volk_free(buffer);
    }

    void setFormat(rawFormat format) {
        _format = format;
        sampleSize = rawFormat_size[format];
        rewind();
    }

    void setChannelCount(int channelcount) {
        _channelcount = channelcount;
        rewind();
    }

    void setWAV(bool useWAV) {
        if (!useWAV) {
            startOffset = 0;
            rewind();
            return;
        }

        startOffset = 0;

        file.seekg(0, file.end);
        size_t filesize = file.tellg();
        file.seekg(0);

        char buf[4];

        file.seekg(0);
        file.read(buf, 4);
        if (memcmp(buf, "RIFF", 4)) {
            rewind();
            return;
        }
        file.seekg(8);
        file.read(buf, 4);
        if (memcmp(buf, "WAVE", 4)) {
            rewind();
            return;
        }
        file.seekg(12);
        file.read(buf, 4);
        if (memcmp(buf, "fmt ", 4)) {
            rewind();
            return;
        }

        size_t size = 0;

        do {
            file.read(buf, 4);
            if (memcmp(buf, "data", 4) == 0) {
                startOffset = (size_t)file.tellg() + 4;
                if (startOffset > (filesize - 1)) {
                    startOffset = 0;
                }
                break;
            }
            size += 4;
            if (size > 1000000) {
                break;
            }
        } while (file.gcount() == 4);

        rewind();
    }

    void readSamples(dsp::complex_t* data, size_t count) {
        size_t readSize = (count * 2) * sampleSize;

        size_t neededBufferSize = readSize;
        if (_channelcount == 1) {
            readSize = (count)*sampleSize;
            neededBufferSize = readSize;
            neededBufferSize += count * 2 * sizeof(float); // extra space for float buffer
        }

        if (bufferSize != neededBufferSize) {
            bufferSize = neededBufferSize;
            volk_free(buffer);
            buffer = volk_malloc(bufferSize, volk_get_alignment());
        }


        file.read((char*)buffer, readSize);
        int read = file.gcount();
        if (read < readSize) {
            file.clear();
            file.seekg(startOffset);
            file.read(&((char*)buffer)[read], readSize - read);
        }

        dsp::complex_t* targetBuffer = data;
        if (_channelcount == 1) {
            targetBuffer = (dsp::complex_t*)(((char*)buffer) + (readSize));
        }

        switch (_format) {
        case rawFormat::u8:
            u8_convert_32f((float*)targetBuffer, (uint8_t*)buffer, readSize / sampleSize);
            break;
        case rawFormat::s8:
            volk_8i_s32f_convert_32f((float*)targetBuffer, (int8_t*)buffer, 128, readSize / sampleSize);
            break;
        case rawFormat::s16:
            volk_16i_s32f_convert_32f((float*)targetBuffer, (int16_t*)buffer, 32768.0f, readSize / sampleSize);
            break;
        case rawFormat::f32:
            memcpy(targetBuffer, buffer, readSize);
            break;
        }

        if (_channelcount == 1) {
            memset(((char*)buffer) + readSize + (count * sizeof(float)), 0, count * sizeof(float));
            volk_32f_x2_interleave_32fc((lv_32fc_t*)data, (float*)(((char*)buffer) + readSize), (float*)(((char*)buffer) + readSize + (count * sizeof(float))), count);
        }
    }

    void rewind() {
        file.seekg(startOffset);
    }

    void close() {
        file.close();
    }

private:
    std::ifstream file;

    int _channelcount = 1;

    rawFormat _format = rawFormat::f32;

    size_t sampleSize = 4;

    void* buffer;
    size_t bufferSize;

    size_t startOffset = 0;

    std::map<rawReader::rawFormat, size_t> rawFormat_size{ { rawFormat::u8, 1 }, { rawFormat::s8, 1 }, { rawFormat::s16, 2 }, { rawFormat::f32, 4 } };

    void u8_convert_32f(float* output, const uint8_t* input, unsigned int points) {
        for (unsigned int i = 0; i < points; i++) {
            output[i] = ((float)input[i] / 127.5) - 1;
        }
    }
};
