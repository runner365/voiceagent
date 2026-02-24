#ifndef VIDEO_FILTER_H
#define VIDEO_FILTER_H

#include "filter_base.h"
#include "ffmpeg_include.h"
#include "utils/logger.hpp"

class VideoFilter : public FilterBase {
public:
    // Video-specific initialization parameters
    struct Params {
        int width;                  // Input video width
        int height;                 // Input video height
        AVPixelFormat pix_fmt;      // Input pixel format
        AVRational time_base;       // Time base of input stream
        AVRational sample_aspect;   // Pixel aspect ratio
    };

    VideoFilter(cpp_streamer::Logger* logger);
    virtual ~VideoFilter();

    // Initialize video filter with specific parameters
    void SetParams(const Params& params);

    // Override base class Init to enforce video-specific parameters
    int Init(const char* filter_desc) override;
	void Release() override;

private:
    Params params_; // Store video-specific parameters
};

#endif // VIDEO_FILTER_H