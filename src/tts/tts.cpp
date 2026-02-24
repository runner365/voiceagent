#include "tts.hpp"

#include "config/config.hpp"
#include "sherpa-onnx/c-api/cxx-api.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <fstream>
#include <sys/stat.h>

namespace cpp_streamer
{

bool CheckFileExist(const std::string& filename) {
    std::ifstream file(filename);
    return file.good();
}

bool CheckDirExist(const std::string& dirname) {
    struct stat info;
    return (stat(dirname.c_str(), &info) == 0 && S_ISDIR(info.st_mode));
}

SherpaOnnxTTSImpl::SherpaOnnxTTSImpl(Logger* logger) : logger_(logger) {
    LogInfof(logger_, "SherpaOnnxTTSImpl created");
}

SherpaOnnxTTSImpl::~SherpaOnnxTTSImpl() {
    LogInfof(logger_, "SherpaOnnxTTSImpl destroyed");
    Release();
}

int SherpaOnnxTTSImpl::Init() {
    auto& tts_cfg = Config::Instance().tts_config;
    tts_enabled_ = tts_cfg.tts_enable;
    if (!tts_enabled_) {
        LogInfof(logger_, "SherpaOnnxTTSImpl is disabled by configuration");
        return 0;
    }

    if (tts_) {
        return 0;
    }

    if (tts_cfg.acoustic_model.empty() || tts_cfg.lexicon.empty() ||
        tts_cfg.tokens.empty()) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl configuration is incomplete: acoustic_model=%s, lexicon=%s, tokens=%s",
                  tts_cfg.acoustic_model.c_str(), tts_cfg.lexicon.c_str(),
                  tts_cfg.tokens.c_str());
        return -1;
    }

    LogInfof(logger_, "SherpaOnnxTTSImpl initializing with acoustic_model=%s,\r\nvocoder=%s,\r\n lexicon=%s,\r\n tokens=%s,\r\n dict_dir=%s,\r\n num_threads=%d",
             tts_cfg.acoustic_model.c_str(), tts_cfg.vocoder.c_str(),
             tts_cfg.lexicon.c_str(), tts_cfg.tokens.c_str(),
             tts_cfg.dict_dir.c_str(), tts_cfg.num_threads);

    if (!CheckFileExist(tts_cfg.acoustic_model)) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl acoustic_model file not found: %s", tts_cfg.acoustic_model.c_str());
        return -1;
    }
    if (!CheckFileExist(tts_cfg.vocoder)) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl vocoder file not found: %s", tts_cfg.vocoder.c_str());
        return -1;
    }
    if (!CheckDirExist(tts_cfg.dict_dir)) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl dict_dir not found: %s", tts_cfg.dict_dir.c_str());
        return -1;
    }
    if (!CheckFileExist(tts_cfg.lexicon)) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl lexicon file not found: %s", tts_cfg.lexicon.c_str());
        return -1;
    }
    if (!CheckFileExist(tts_cfg.tokens)) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl tokens file not found: %s", tts_cfg.tokens.c_str());
        return -1;
    }

    sherpa_onnx::cxx::OfflineTtsConfig config;
    auto& matcha = config.model.matcha;
    matcha.acoustic_model = tts_cfg.acoustic_model;
    matcha.vocoder = tts_cfg.vocoder;
    matcha.lexicon = tts_cfg.lexicon;
    matcha.tokens = tts_cfg.tokens;
    matcha.dict_dir = tts_cfg.dict_dir;
    config.model.num_threads = std::max<int32_t>(1, tts_cfg.num_threads);
    config.model.provider = "cpu";
    config.model.debug = 0;

    try {
        auto offline_tts = sherpa_onnx::cxx::OfflineTts::Create(config);
        tts_.reset(new sherpa_onnx::cxx::OfflineTts(std::move(offline_tts)));
        sample_rate_ = tts_->SampleRate();
    } catch (const std::exception& e) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl failed to create sherpa-onnx offline TTS: %s", e.what());
        tts_.reset();
        return -1;
    }

    LogInfof(logger_, "SherpaOnnxTTSImpl initialized, sample_rate=%d", sample_rate_);
    return 0;
}

void SherpaOnnxTTSImpl::Release() {
    if (tts_) {
        tts_.reset();
        sample_rate_ = 0;
    }
    tts_enabled_ = false;
}

int SherpaOnnxTTSImpl::SynthesizeText(const std::string& text, int32_t& sample_rate,
                            std::vector<float>& audio_data) {
    sample_rate = 0;
    audio_data.clear();
    if (!tts_enabled_) {
        LogWarnf(logger_, "SherpaOnnxTTSImpl is disabled; cannot synthesize text");
        return -1;
    }
    if (!tts_) {
        LogWarnf(logger_, "SherpaOnnxTTSImpl is not initialized");
        return -1;
    }
    if (text.empty()) {
        LogWarnf(logger_, "SherpaOnnxTTSImpl invoked with empty text");
        return -1;
    }

    try {
        auto generated = tts_->Generate(text);
        sample_rate = generated.sample_rate;
        audio_data = std::move(generated.samples);
        return 0;
    } catch (const std::exception& e) {
        LogErrorf(logger_, "SherpaOnnxTTSImpl failed to synthesize text: %s", e.what());
        return -1;
    }
}

}