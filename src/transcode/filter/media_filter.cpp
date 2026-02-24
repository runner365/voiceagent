#include "media_filter.h"
#include "utils/uuid.hpp"

using namespace cpp_streamer;
MediaFilter::MediaFilter(Logger* logger) : logger_(logger) {
    id_ = UUID::MakeUUID2();
    LogInfof(logger_, "MediaFilter constructed, id:%s", id_.c_str());
}

MediaFilter::~MediaFilter() {
    Release();
}

int MediaFilter::InitVideoFilter(const VideoFilter::Params& params, const char* filter_desc) {
    if (video_filter_inited_) {
        return 0;
    }

    video_filter_ptr_ = std::make_unique<VideoFilter>(logger_);
    if (!video_filter_ptr_) {
        LogErrorf(logger_, "InitVideoFilter() failed: could not create VideoFilter instance");
        return -1;
    }
    video_filter_ptr_->SetParams(params);
    video_filter_ptr_->SetId(id_);
	LogInfof(logger_, "InitVideoFilter() filter_desc: %s", filter_desc);
    int ret = video_filter_ptr_->Init(filter_desc);
    if (ret < 0) {
        LogErrorf(logger_, "InitVideoFilter() failed: could not initialize video filter");
        video_filter_ptr_.reset();
        return ret;
    }
    video_filter_ptr_->SetSinkCallback(sink_cb_);
	video_filter_inited_ = true;
    return 0;
}

int MediaFilter::InitAudioFilter(const AudioFilter::Params& params, const char* filter_desc) {
    if (audio_filter_inited_) {
        return 0;
	}
    audio_filter_ptr_ = std::make_unique<AudioFilter>(logger_);
    if (!audio_filter_ptr_) {
        LogErrorf(logger_, "InitAudioFilter() failed: could not create AudioFilter instance");
        return -1;
    }
    audio_filter_ptr_->SetParams(params);
    audio_filter_ptr_->SetId(id_);
    int ret = audio_filter_ptr_->Init(filter_desc);
    if (ret < 0) {
        LogErrorf(logger_, "InitAudioFilter() failed: could not initialize audio filter");
        audio_filter_ptr_.reset();
        return ret;
    }
    audio_filter_ptr_->SetSinkCallback(sink_cb_);
    audio_filter_inited_ = true;
    return 0;
}

void MediaFilter::Release() {
    if (audio_filter_inited_) {
        audio_filter_inited_ = false;
        if (audio_filter_ptr_) {
            audio_filter_ptr_->SetSinkCallback(nullptr);
			audio_filter_ptr_->Release();
			audio_filter_ptr_.reset();
        }
	}
    if (video_filter_inited_) {
        video_filter_inited_ = false;
        if (video_filter_ptr_) {
            video_filter_ptr_->SetSinkCallback(nullptr);
            video_filter_ptr_->Release();
            video_filter_ptr_.reset();
        }
	}
}

void MediaFilter::OnData(std::shared_ptr<FFmpegMediaPacket> pkt) {
    if (!pkt || !pkt->IsAVFrame()) {
        LogWarnf(logger_, "OnData() warning: invalid packet");
        return;
    }
    AVFrame* frame = pkt->GetAVFrame();
    if (!frame) {
        LogWarnf(logger_, "OnData() warning: packet has null AVFrame");
        return;
    }
    if (pkt->GetMediaPktType() == MEDIA_VIDEO_TYPE) {
        InputVideoFrame(frame);
    } else if (pkt->GetMediaPktType() == MEDIA_AUDIO_TYPE) {
        InputAudioFrame(frame);
    } else {
        LogWarnf(logger_, "OnData() warning: unknown packet type %d", (int)pkt->GetMediaPktType());
    }
}

int MediaFilter::InputVideoFrame(AVFrame* frame) {
    if (!video_filter_inited_) {
        return 0;
    }
    if (!video_filter_ptr_) {
        LogWarnf(logger_, "InputVideoFrame() warning: video filter not initialized");
        return -1;
    }
    return video_filter_ptr_->InputFrame(frame, MEDIA_VIDEO_TYPE);
}

int MediaFilter::InputAudioFrame(AVFrame* frame) {
    if (!audio_filter_inited_) {
        return 0;
	}
    if (!audio_filter_ptr_) {
        LogWarnf(logger_, "InputAudioFrame() warning: audio filter not initialized");
        return -1;
    }
    return audio_filter_ptr_->InputFrame(frame, MEDIA_AUDIO_TYPE);
}

int MediaFilter::FlushVideo() {
    if (!video_filter_ptr_) {
        LogWarnf(logger_, "FlushVideo() warning: video filter not initialized");
        return -1;
    }
    return video_filter_ptr_->Flush();
}

int MediaFilter::FlushAudio() {
    if (!audio_filter_ptr_) {
        LogWarnf(logger_, "FlushAudio() warning: audio filter not initialized");
        return -1;
    }
    return audio_filter_ptr_->Flush();
}
