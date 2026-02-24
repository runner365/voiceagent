#include "video_filter.h"

using namespace cpp_streamer;
VideoFilter::VideoFilter(Logger* logger) : FilterBase(logger) {
    //init the default params
    params_.width = 1280;
    params_.height = 720;
    params_.pix_fmt = AV_PIX_FMT_YUV420P;
    params_.time_base = AVRational{1, 30};
    params_.sample_aspect = AVRational{1, 1};

	pkt_type_ = MEDIA_VIDEO_TYPE;
}
VideoFilter::~VideoFilter() {
}

void VideoFilter::SetParams(const Params& params) {
    params_ = params;
}

int VideoFilter::Init(const char* filter_desc) {
    if (inited_) {
        return 0;
	}
    int ret = 0;
    // Format buffer source arguments for video
    char args[512];
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            params_.width, params_.height, params_.pix_fmt,
            params_.time_base.num, params_.time_base.den,
            params_.sample_aspect.num, params_.sample_aspect.den);

    // Get video-specific buffer filters
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");

    // Delegate to base class to complete filter graph initialization
    ret = InitFilterGraph(filter_desc, buffersrc, buffersink, args);
    if (ret < 0) {
        LogErrorf(logger_, "InitFilterGraph() failed");
        return ret;
	}
	inited_ = true;
    return 0;
}

void VideoFilter::Release() {
    if (!inited_) {
		return;
	}
    Cleanup();
    inited_ = false;
}
    