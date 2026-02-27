#ifndef TTS_HPP
#define TTS_HPP

#include "utils/logger.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"
#include <memory>

namespace sherpa_onnx {
namespace cxx {
class OfflineTts;
}
}

namespace cpp_streamer
{
class SherpaOnnxTTSImpl
{
public:
    SherpaOnnxTTSImpl(Logger* logger);
    virtual ~SherpaOnnxTTSImpl();

public:
    int Init();
    int SynthesizeText(const std::string& text, int32_t& sample_rate, std::vector<float>& audio_data);
    void Release();

private:
    Logger* logger_ = nullptr;
    std::unique_ptr<sherpa_onnx::cxx::OfflineTts> tts_;
    bool init_ = false;
    int32_t sample_rate_ = 0;
};

}

#endif