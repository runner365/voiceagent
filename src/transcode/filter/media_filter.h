#ifndef MEDIA_FILTER_H
#define MEDIA_FILTER_H

#include "ffmpeg_include.h"
#include "audio_filter.h"
#include "video_filter.h"
#include "utils/logger.hpp"
#include <memory>

class MediaFilter : public SinkCallbackI
{
public:
    MediaFilter(cpp_streamer::Logger* logger);
    virtual ~MediaFilter();

public:
    int InitVideoFilter(const VideoFilter::Params& params, const char* filter_desc);
    int InitAudioFilter(const AudioFilter::Params& params, const char* filter_desc);
    void Release();

    void SetSinkCallback(SinkCallbackI* callback) {
        sink_cb_ = callback;
    }
    std::string GetId() const {
        return id_;
    }

public:
    virtual void OnData(std::shared_ptr<FFmpegMediaPacket> pkt) override;

private:
    int InputVideoFrame(AVFrame* frame);
    int InputAudioFrame(AVFrame* frame);

    int FlushVideo();
    int FlushAudio();

private:
    cpp_streamer::Logger* logger_ = nullptr;
    std::unique_ptr<VideoFilter> video_filter_ptr_;
    std::unique_ptr<AudioFilter> audio_filter_ptr_;
	bool video_filter_inited_ = false;
	bool audio_filter_inited_ = false;
    SinkCallbackI* sink_cb_ = nullptr;
    std::string id_;
};

#endif