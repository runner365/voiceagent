#ifndef PCM2OPUS_HPP_
#define PCM2OPUS_HPP_
#include "utils/logger.hpp"
#include "transcode/filter/media_filter.h"
#include "transcode/encoder/encoder.h"
#include "transcode/ffmpeg_include.h"
#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace cpp_streamer
{

class PCM_DATA_INFO
{
public:
    PCM_DATA_INFO() = default;
    PCM_DATA_INFO(const std::vector<float>& data, int sample_rate, int channels)
        : pcm_float_data(data), sample_rate(sample_rate), channels(channels) {};
    ~PCM_DATA_INFO() = default;

    std::vector<float> pcm_float_data;
    int sample_rate = 0;
    int channels = 0;
};

class Pcm2OpusCallbackI
{
public:
    virtual void OnOpusData(const std::vector<uint8_t>& opus_data, int sample_rate, int channels, int64_t pts, int task_index) = 0;
};

class Pcm2Opus : public SinkCallbackI
{
public:
    Pcm2Opus(Pcm2OpusCallbackI* cb, Logger* logger);
    virtual ~Pcm2Opus();

public:
    void InsertPcmData(const PCM_DATA_INFO& pcm_data);

protected:
    virtual void OnData(std::shared_ptr<FFmpegMediaPacket> pkt) override;

private:
    void OnWorkerThread();
    void StartWorkerThread();
    void StopWorkerThread();

private:
    void AddPcmQueue(const PCM_DATA_INFO& pcm_data);
    PCM_DATA_INFO GetPcmFromQueue();
    size_t GetPcmQueueSize();

private:
    void HandleFrameInFilter(AVFrame* frame);
    void HandleFrameInEncoder(AVFrame* frame);

private:
    Pcm2OpusCallbackI* cb_ = nullptr;
    Logger* logger_ = nullptr;

private:
    std::unique_ptr<MediaFilter> pcm_filter_;
    std::unique_ptr<Encoder> opus_encoder_;
    int64_t next_audio_pts_ = 0;

private:
    std::queue<PCM_DATA_INFO> pcm_data_queue_;
    std::mutex pcm_data_mutex_;
    std::condition_variable pcm_data_cv_;
    std::unique_ptr<std::thread> encode_thread_ptr_;
    bool encode_thread_running_ = false;
    int current_index_ = 0;
};

}

#endif // PCM2OPUS_HPP_