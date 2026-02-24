#include "filter_base.h"
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <iostream>

using namespace cpp_streamer;

FilterBase::FilterBase(Logger* logger) : logger_(logger) {
}

FilterBase::~FilterBase() {
    Cleanup();
}

int FilterBase::InitFilterGraph(const char* filter_desc, 
                              const AVFilter* buffersrc, 
                              const AVFilter* buffersink,
                              const char* buffersrc_args) {
    int ret = 0;
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();

    // Create filter graph
    filter_graph_ = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph_) {
        ret = AVERROR(ENOMEM);
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to allocate filter graph or in/out: %s", errbuf);
        goto cleanup;
    }

    // Create buffer source context
    ret = avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                      buffersrc_args, nullptr, filter_graph_);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to create buffer source: %s", errbuf);
        goto cleanup;
    }

    ret = avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
        nullptr, nullptr, filter_graph_);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to create buffer sink: %s", errbuf);
        goto cleanup;
    }
    // Configure filter graph inputs/outputs
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    // Parse filter description
    ret = avfilter_graph_parse_ptr(filter_graph_, filter_desc,
                                  &inputs, &outputs, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to parse filter description error: %s, filter_desc:%s", errbuf, filter_desc);
        goto cleanup;
    }

    // Validate filter graph configuration
    ret = avfilter_graph_config(filter_graph_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to configure filter graph error: %s, filter_desc:%s", errbuf, filter_desc);
        goto cleanup;
    }

cleanup:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}

int FilterBase::InputFrame(AVFrame* frame, MEDIA_PKT_TYPE pkt_type) {
    if (!inited_) {
        LogErrorf(logger_, "FilterBase::InputFrame() failed: filter not initialized");
        return -1;
	}

    if (!frame || !buffersrc_ctx_) return AVERROR(EINVAL);

    // Send frame to buffer source
    int ret = av_buffersrc_add_frame_flags(buffersrc_ctx_, frame, 0);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to send frame to filter graph: %s", errbuf);
        return ret;
    }

    // Pull processed frames from buffer sink
    while (true) {
        AVFrame* filtered_frame = av_frame_alloc();
        if (!filtered_frame) {
            LogErrorf(logger_, "Failed to allocate filtered frame");
            return AVERROR(ENOMEM);
        }

        ret = av_buffersink_get_frame(buffersink_ctx_, filtered_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&filtered_frame);
            break; // No frame ready; caller should retry later
        }
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LogErrorf(logger_, "Failed to get filtered frame: %s", errbuf);
            av_frame_free(&filtered_frame);
            return ret;
        }
        AVRational filter_tb = av_buffersink_get_time_base(buffersink_ctx_);
        if (filter_tb.den > 0 && filter_tb.num > 0) {
            filtered_frame->time_base = filter_tb;
        }
		
        // Invoke output callback with processed frame
        if (sink_cb_) {
            auto pkt_ptr = std::shared_ptr<FFmpegMediaPacket>(new FFmpegMediaPacket(filtered_frame, pkt_type));
            pkt_ptr->SetId(id_);
            sink_cb_->OnData(pkt_ptr);
        } else {
            av_frame_free(&filtered_frame);
        }
    }

    return 0;
}

int FilterBase::Flush() {
    if (!buffersrc_ctx_) return AVERROR(EINVAL);

    // Flush buffer source
    int ret = av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, AV_BUFFERSRC_FLAG_PUSH);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to flush filter graph: %s", errbuf);
        return ret;
    }

    // Process remaining frames
    return InputFrame(nullptr, pkt_type_);
}

void FilterBase::Cleanup() {
    if (filter_graph_) {
        avfilter_graph_free(&filter_graph_);
        filter_graph_ = nullptr;
    }
    buffersrc_ctx_ = nullptr;
    buffersink_ctx_ = nullptr;
}
