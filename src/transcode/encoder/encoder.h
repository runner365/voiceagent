#ifndef ENCODER_H
#define ENCODER_H
#include "ffmpeg_include.h"
#include "utils/logger.hpp"
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>

using namespace cpp_streamer;
class Encoder : public SinkCallbackI
{
public:
    Encoder(Logger* logger);
    virtual ~Encoder();

public:
    int OpenVideoEncoder(const VideoEncInfo& enc_info);
    int OpenAudioEncoder(const AudioEncInfo& enc_info, const char* codec_name = nullptr);
    void CloseVideoEncoder();
    void CloseAudioEncoder();
    std::string GetId() const { return id_; }
    
public:
    virtual void OnData(std::shared_ptr<FFmpegMediaPacket> pkt) override;

public:
    void SetSinkCallback(SinkCallbackI* cb);
    int InputFrame(std::shared_ptr<FFmpegMediaPacket> frame);

private:
    int InsertFrameToQueue(std::shared_ptr<FFmpegMediaPacket> frame);
    std::shared_ptr<FFmpegMediaPacket> GetFrameFromQueue();
	size_t GetFrameQueueSize();

private:
    void StartEncodeThread();
    void StopEncodeThread();
    void EncodeThread();
    int HandleVideoEncodedPacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr);
    int HandleAudioEncodedPacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr);
    int DoVideoEncode(AVFrame* frame);
    void FlushVideoFrame();
	void FlushAudioFrame();

private:
	int InitAudioFifo();
    void ReleaseAudioFifo();
	int AddSamplesToFifo(AVFrame* frame);
	size_t GetSamplesFromFifo(AVFrame* input_frame, std::vector<AVFrame*>& frames);
    AVFrame* GetNewAudioFrame(uint8_t*& frame_buf);

private:
    std::string id_;
    Logger* logger_ = nullptr;
    AVCodecContext* video_codec_ctx_ = nullptr;
	AVCodecContext* audio_codec_ctx_ = nullptr;
    SinkCallbackI* sink_cb_ = nullptr;
	AVAudioFifo* audio_fifo_ = nullptr;
    int64_t last_audio_pts_ = -1;
    int64_t last_video_dts_ = -1;
    int64_t last_vframe_pts_ = -1;
	bool first_video_frame_ = true;

private://async thread
    std::queue<std::shared_ptr<FFmpegMediaPacket>> frame_queue_;
    std::mutex frame_mutex_;
    std::condition_variable frame_cond_;
    std::unique_ptr<std::thread> encode_thread_ptr_;
    bool thread_running_ = false;
};
#endif