#ifndef AUDIO_FILTER_H
#define AUDIO_FILTER_H

#include "ffmpeg_include.h"
#include "filter_base.h"
#include "utils/logger.hpp"

class AudioFilter : public FilterBase {
public:
    // Audio-specific initialization parameters
    struct Params {
        int sample_rate;            // Input sample rate (Hz)
        AVChannelLayout ch_layout;  // Input channel layout
        AVSampleFormat sample_fmt;  // Input sample format
        AVRational time_base;       // Time base of input stream
    };

    AudioFilter(cpp_streamer::Logger* logger);
    virtual ~AudioFilter();

    // Initialize audio filter with specific parameters
    void SetParams(const Params& params);

public:
    // Override base class Init to enforce audio-specific parameters
    int Init(const char* filter_desc) override;
	void Release() override;

private:
    Params params_; // Store audio-specific parameters
};

#endif // AUDIO_FILTER_H
    