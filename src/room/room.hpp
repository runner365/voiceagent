#ifndef ROOM_HPP
#define ROOM_HPP
#include "utils/logger.hpp"
#include "utils/data_buffer.hpp"
#include "transcode/decoder/decoder.h"
#include "transcode/filter/media_filter.h"
#include "room_pub.hpp"
#include "transcode/pcm2opus.hpp"
#include "AIUser.hpp"

namespace cpp_streamer {

class Room : public SinkCallbackI, public Pcm2OpusCallbackI
{
public:
    Room(const std::string& room_id, RoomCallbackI* cb, Logger* logger);
    virtual ~Room();

public:
    std::string GetRoomId() const { return room_id_; }
    void Close();
    bool IsAlive() const;

public:
    void OnHanldeOpusData(const std::string& user_id, DATA_BUFFER_PTR data_ptr);
    void OnHandleResponseText(const std::string& user_id, const std::string& text);

public:
    virtual void OnOpusData(const std::vector<uint8_t>& opus_data, int sample_rate, int channels, int64_t pts, int task_index) override;

public://implement SinkCallbackI
    virtual void OnData(std::shared_ptr<FFmpegMediaPacket> pkt) override;

private:
    void SendPcmData2VoiceAgent(const std::string& user_id, DATA_BUFFER_PTR data_ptr);

private:
    std::string room_id_;
    std::string user_id_;
    int64_t last_input_ms_ = 0;
    Logger* logger_ = nullptr;
    RoomCallbackI* cb_ = nullptr;

private:
    bool closed_ = false;
    std::unique_ptr<Decoder> audio_decoder_ptr_;
    std::unique_ptr<MediaFilter> audio_filter_ptr_;

private:
    std::unique_ptr<AIUser> ai_user_ptr_;
};

}

#endif