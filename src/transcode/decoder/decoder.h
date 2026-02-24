#ifndef DECODER_H
#define DECODER_H
#include "ffmpeg_include.h"
#include "utils/logger.hpp"
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

class Decoder : public SinkCallbackI
{
public:
    Decoder(cpp_streamer::Logger* logger);
    virtual ~Decoder();

public:
    int InputPacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr, bool async = false);
    void SetSinkCallback(SinkCallbackI* cb);
    std::string GetId() const { return id_; }
    void CloseDecoder();
    
public:
    virtual void OnData(std::shared_ptr<FFmpegMediaPacket> pkt) override;

private:
    int DecodePacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr);
    int OpenDecoder(AVCodecParameters* params);
    int OpenDecoder(AVCodecID codec_id);

private:
    int InsertPacketToQueue(std::shared_ptr<FFmpegMediaPacket> pkt_ptr);
    std::shared_ptr<FFmpegMediaPacket> GetPacketFromQueue();
    void StartDecodeThread();
    void StopDecodeThread();
    void DecodeThread();

private:
    std::string id_;
    cpp_streamer::Logger* logger_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;

private:
    SinkCallbackI* sink_cb_ = nullptr;

private://async thread
    std::queue<std::shared_ptr<FFmpegMediaPacket>> pkt_queue_;
    std::mutex pkt_mutex_;
    std::condition_variable pkt_cond_;
    std::unique_ptr<std::thread> decode_thread_ptr_;
    bool thread_running_ = false;
};

#endif