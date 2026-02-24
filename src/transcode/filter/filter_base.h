#ifndef FILTER_BASE_H
#define FILTER_BASE_H

#include "ffmpeg_include.h"
#include "utils/av/av.hpp"
#include "utils/logger.hpp"
#include <functional>

// Base class for audio/video filters
class FilterBase {
public:
    FilterBase(cpp_streamer::Logger* logger);
    virtual ~FilterBase();

    // Delete copy constructor and assignment to prevent resource issues
    FilterBase(const FilterBase&) = delete;
    FilterBase& operator=(const FilterBase&) = delete;

    // Initialize filter graph (pure virtual - implemented by subclasses)
    virtual int Init(const char* filter_desc) = 0;
    virtual void Release() = 0;

    // Send a frame to the filter graph for processing
    virtual int InputFrame(AVFrame* frame, cpp_streamer::MEDIA_PKT_TYPE pkt_type);

    // Flush remaining frames in the filter graph
    virtual int Flush();

public:
    void SetSinkCallback(SinkCallbackI* callback) {
        sink_cb_ = callback;
    }
    void SetId(const std::string& id) {
        id_ = id;
    }
    std::string GetId() const {
        return id_;
    }

protected:
    // Common initialization helper (called by subclasses)
    int InitFilterGraph(const char* filter_desc, 
                       const AVFilter* buffersrc, 
                       const AVFilter* buffersink,
                       const char* buffersrc_args);

    // Release all allocated resources
    void Cleanup();

protected:
    cpp_streamer::Logger* logger_ = nullptr;                     // Logger instance
    AVFilterGraph* filter_graph_ = nullptr;       // Shared filter graph
    AVFilterContext* buffersrc_ctx_ = nullptr;    // Input buffer filter
    AVFilterContext* buffersink_ctx_ = nullptr;   // Output buffer filter
	cpp_streamer::MEDIA_PKT_TYPE pkt_type_ = cpp_streamer::MEDIA_UNKNOWN_TYPE; // Packet type (audio/video)
    bool inited_ = false;
    std::string id_;

private:
    SinkCallbackI* sink_cb_ = nullptr;
    
};

#endif // FILTER_BASE_H
    