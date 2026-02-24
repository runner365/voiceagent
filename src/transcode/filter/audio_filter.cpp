#include "audio_filter.h"

using namespace cpp_streamer;
AudioFilter::AudioFilter(Logger* logger) : FilterBase(logger) {
    //init the default params
    params_.sample_fmt = AV_SAMPLE_FMT_FLTP;
    params_.sample_rate = 44100;
    params_.ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
    params_.ch_layout.nb_channels = 2;
    params_.ch_layout.u.mask = AV_CH_LAYOUT_STEREO;
    params_.time_base = AVRational{1, params_.sample_rate};

	pkt_type_ = MEDIA_AUDIO_TYPE;
}

AudioFilter::~AudioFilter() {
}

void AudioFilter::SetParams(const Params& params) {
    params_ = params;
}

int AudioFilter::Init(const char* filter_desc) {
    if (inited_) {
        LogWarnf(logger_, "AudioFilter::Init() warning: already initialized, skipping");
        return 0;
	}
    // Format buffer source arguments for audio
    char args[512];
    snprintf(args, sizeof(args),
            "sample_rate=%d:sample_fmt=%d:channel_layout=0x%llx:time_base=%d/%d",
            params_.sample_rate, params_.sample_fmt,
            (unsigned long long)params_.ch_layout.u.mask,
            params_.time_base.num, params_.time_base.den);

    // Get audio-specific buffer filters
    const AVFilter* buffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* buffersink = avfilter_get_by_name("abuffersink");

    // Delegate to base class to complete filter graph initialization
    int ret = InitFilterGraph(filter_desc, buffersrc, buffersink, args);
    if (ret < 0) {
        LogErrorf(logger_, "AudioFilter::Init() failed: InitFilterGraph error");
        return ret;
	}
    inited_ = true;
    return ret;
}

void AudioFilter::Release() {
    if (!inited_) {
        return;
    }
    inited_ = false;
    Cleanup();
}