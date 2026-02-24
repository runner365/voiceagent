#include "room.hpp"
#include "utils/base64.hpp"
#include "utils/timeex.hpp"

namespace cpp_streamer {

Room::Room(const std::string& room_id, RoomCallbackI* cb, Logger* logger) : room_id_(room_id), logger_(logger) {
    cb_ = cb;
    last_input_ms_ = now_millisec();
    LogInfof(logger_, "Room %s created", room_id_.c_str()); 
}

Room::~Room() {
    LogInfof(logger_, "Room %s destroyed", room_id_.c_str());
    Close();
}

void Room::OnHandleResponseText(const std::string& user_id, const std::string& text) {
    LogInfof(logger_, "Room %s Handle Response Text user_id: %s, text: %s", 
        room_id_.c_str(), user_id.c_str(), text.c_str());

    try {
        if (!ai_user_ptr_) {
            ai_user_ptr_.reset(new AIUser(user_id, this, logger_));
        }
        ai_user_ptr_->InputText(text);
    } catch (const std::exception& e) {
        LogErrorf(logger_, "Room %s Handle Response Text user_id: %s, text: %s, exception: %s", 
            room_id_.c_str(), user_id.c_str(), text.c_str(), e.what());
    }
}

void Room::OnHanldeOpusData(const std::string& user_id, DATA_BUFFER_PTR data_ptr) {
    LogDebugf(logger_, "Room %s Handle user input  Opus Data user_id: %s, data_len: %zu", 
        room_id_.c_str(), user_id.c_str(), data_ptr->DataLen());
    user_id_ = user_id;
    if (!audio_decoder_ptr_) {
        audio_decoder_ptr_.reset(new Decoder(logger_));
        audio_decoder_ptr_->SetSinkCallback(this);
    }
    last_input_ms_ += 20;

    int64_t dts = last_input_ms_ * 48000 / 1000;
    int64_t pts = dts;

    AVPacket* av_pkt = GenerateAVPacket((uint8_t*)data_ptr->Data(), 
        data_ptr->DataLen(), pts, dts, AV_PACKET_TYPE_DEF_AUDIO, {1, 48000});
    std::shared_ptr<FFmpegMediaPacket> media_pkt_ptr;
    media_pkt_ptr.reset(new FFmpegMediaPacket(av_pkt, MEDIA_AUDIO_TYPE));
    FFmpegMediaPacketPrivate prv;
    prv.private_type_ = PRIVATE_DATA_TYPE_DECODER_ID;
    prv.codec_id_ = AV_CODEC_ID_OPUS;
    
    media_pkt_ptr->SetPrivateData(prv);

    audio_decoder_ptr_->OnData(media_pkt_ptr);
}

void Room::Close() {
    if (closed_) {
        return;
    }
    LogInfof(logger_, "Room %s closed", room_id_.c_str());
    closed_ = true;

    if (audio_decoder_ptr_) {
        audio_decoder_ptr_->CloseDecoder();
        audio_decoder_ptr_.reset();
    }
    if (audio_filter_ptr_) {
        audio_filter_ptr_.reset();
    }
}

bool Room::IsAlive() const {
    if (closed_) {
        return false;
    }
    int64_t now_ms = now_millisec();
    return (now_ms - last_input_ms_) < 60*1000;
}
void Room::OnData(std::shared_ptr<FFmpegMediaPacket> pkt) {
    if (closed_) {
        return;
    }
    if (pkt->GetId() == audio_decoder_ptr_->GetId()) {
        //decode avframe
        if (!pkt || !pkt->IsAVFrame()) {
            return;
        }
        AVFrame* frame = pkt->GetAVFrame();
        enum AVSampleFormat sample_fmt = (enum AVSampleFormat)frame->format;

        if (!audio_filter_ptr_) {
            audio_filter_ptr_.reset(new MediaFilter(logger_));
            audio_filter_ptr_->SetSinkCallback(this);

            AudioFilter::Params input_param = {
                .sample_rate = frame->sample_rate,
                .ch_layout = frame->ch_layout,
                .sample_fmt = sample_fmt,
                .time_base = {1, frame->sample_rate}
            };
            //filter_desc: change to 16000, single channel, s16 format
            std::string filter_desc = "aresample=16000,asetrate=16000*1.0,aformat=sample_fmts=s16:channel_layouts=mono";
            audio_filter_ptr_->InitAudioFilter(input_param, filter_desc.c_str());
        }
        LogDebugf(logger_, "decoded avframe nb_samples=%d, sample fmt:%s, pts:%ld", 
            frame->nb_samples, av_get_sample_fmt_name(sample_fmt), frame->pts);
        audio_filter_ptr_->OnData(pkt);
        
        return;
    }
    if (pkt->GetId() == audio_filter_ptr_->GetId()) {
        // filtered avframe
        if (!pkt || !pkt->IsAVFrame()) {
            return;
        }
        AVFrame* frame = pkt->GetAVFrame();
        enum AVSampleFormat sample_fmt = (enum AVSampleFormat)frame->format;

        size_t num_samples = frame->nb_samples;
        size_t num_channels = frame->ch_layout.nb_channels;

        DATA_BUFFER_PTR audio_buffer = std::make_shared<DataBuffer>();
        size_t data_size = num_samples * num_channels * av_get_bytes_per_sample(sample_fmt);
        LogDebugf(logger_, "VoiceAgent avfilter audio frame: pts=%ld, sample_rate=%d, format=%s, channels=%d, nb_samples=%d, pts:%ld, data size:%zu",
            frame->pts,
            frame->sample_rate,
            av_get_sample_fmt_name(sample_fmt),
            (int)num_channels,
            (int)num_samples,
            frame->pts,
            data_size
        );
        audio_buffer->AppendData((char*)frame->data[0], data_size);

        SendPcmData2VoiceAgent(user_id_, audio_buffer);

        // write to pcm16 file for testing
        #if 0
        std::ofstream pcm16_file;
        std::string filename = "va_" + pkt->GetId() + ".pcm";
        pcm16_file.open(filename, std::ios::out | std::ios::app | std::ios::binary);
        if (pcm16_file.is_open()) {
            int16_t* data_ptr = (int16_t*)frame->data[0];
            pcm16_file.write((char*)data_ptr, num_samples * num_channels * sizeof(int16_t));
            pcm16_file.close();
        }
        #endif
        return;
    }
    LogWarnf(logger_, "Room OnData() warning: unknown packet id:%s, roomId:%s", 
        pkt->GetId().c_str(), room_id_.c_str());
}

void Room::SendPcmData2VoiceAgent(const std::string& user_id, DATA_BUFFER_PTR data_ptr) {
    if (cb_) {
        std::string msg_base64 = Base64Encode((uint8_t*)data_ptr->Data(), data_ptr->DataLen());
        cb_->Notification2VoiceAgent(std::make_shared<RoomNotificationInfo>("pcm_data", room_id_, user_id, msg_base64));
    }
}

void Room::OnOpusData(const std::vector<uint8_t>& opus_data, int sample_rate, int channels, int64_t pts, int task_index) {
    if (cb_) {
        std::string msg_base64 = Base64Encode((uint8_t*)opus_data.data(), opus_data.size());
        std::shared_ptr<RoomNotificationInfo> info_ptr = std::make_shared<RoomNotificationInfo>("tts_opus_data", room_id_, user_id_, msg_base64);
        info_ptr->task_index = task_index;
        cb_->Notification2VoiceAgent(info_ptr);
    }
}

}