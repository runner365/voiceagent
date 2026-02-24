#include "decoder.h"
#include "utils/logger.hpp"
#include "utils/uuid.hpp"
#include <thread>

using namespace cpp_streamer;

Decoder::Decoder(Logger* logger) {
    logger_ = logger;
    id_ = UUID::MakeUUID2();
	LogInfof(logger_, "Decoder constructed, id:%s", id_.c_str());
}

Decoder::~Decoder() {
    CloseDecoder();
	LogInfof(logger_, "Decoder destructed, id:%s", id_.c_str());
}

void Decoder::SetSinkCallback(SinkCallbackI* cb) {
    sink_cb_ = cb;
}

void Decoder::OnData(std::shared_ptr<FFmpegMediaPacket> pkt) {
    InputPacket(pkt);
}

int Decoder::InputPacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr, bool async) {
    if (!pkt_ptr) {
        return -1;
    }
    if (!pkt_ptr->IsAVPacket()) {
        return -1;
    }
    if (!async) {
        return DecodePacket(pkt_ptr);
    }

    InsertPacketToQueue(pkt_ptr);

    StartDecodeThread();
    return 0;
}

int Decoder::InsertPacketToQueue(std::shared_ptr<FFmpegMediaPacket> pkt_ptr) {
    if (!pkt_ptr) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(pkt_mutex_);
    pkt_queue_.push(pkt_ptr);
    pkt_cond_.notify_one();
    return 0;
}

std::shared_ptr<FFmpegMediaPacket> Decoder::GetPacketFromQueue() {
    std::unique_lock<std::mutex> lock(pkt_mutex_);
    pkt_cond_.wait(lock, [this]() { return !pkt_queue_.empty() || !thread_running_; });
    if (!thread_running_) {
        return nullptr;
    }
    if (pkt_queue_.empty()) {
        return nullptr;
    }
    auto pkt_ptr = pkt_queue_.front();
    pkt_queue_.pop();
    return pkt_ptr;
}

void Decoder::StartDecodeThread() {
    if (thread_running_) {
        return;
    }
    thread_running_ = true;
	LogInfof(logger_, "Starting decoder thread, id:%s", id_.c_str());
    if (!decode_thread_ptr_) {
        decode_thread_ptr_ = std::make_unique<std::thread>(&Decoder::DecodeThread, this);
    }
}

void Decoder::StopDecodeThread() {
    if (!thread_running_) {
        return;
    }
    thread_running_ = false;
    
	LogInfof(logger_, "Stopping decoder thread, id:%s", id_.c_str());
    if (decode_thread_ptr_ && decode_thread_ptr_->joinable()) {
        pkt_cond_.notify_all();
        decode_thread_ptr_->join();
    }
}

void Decoder::DecodeThread() {
    LogInfof(logger_, "Decoder thread started, id:%s", id_.c_str());
    while (thread_running_) {
        auto pkt_ptr = GetPacketFromQueue();
        if (!pkt_ptr) {
            continue;
        }
        DecodePacket(pkt_ptr);
    }
}

int Decoder::DecodePacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr) {
    int ret = 0;

    if (!pkt_ptr) {
        return -1;
    }
    if (!pkt_ptr->IsAVPacket()) {
        return -1;
    }
    FFmpegMediaPacketPrivate priv = pkt_ptr->GetPrivateData();
    if (priv.private_type_ == PRIVATE_DATA_TYPE_UNKNOWN) {
        LogErrorf(logger_, "Decoder DecodePacket() no private data");
        return -1;
    }
    if (priv.private_type_ == PRIVATE_DATA_TYPE_AVCODECPARAMETERS) {
        AVCodecParameters* params = (AVCodecParameters*)priv.private_data_;
        if (!params) {
            return -1;
        }

        ret = OpenDecoder(params);
        if (ret < 0) {
            return -1;
        }
    } else if (priv.private_type_ == PRIVATE_DATA_TYPE_DECODER_ID) {
        ret = OpenDecoder(priv.codec_id_);
        if (ret < 0) {
            return -1;
        }
    }
    
    AVPacket* pkt = pkt_ptr->GetAVPacket();
    if (!pkt) {
        return -1;
    }
    auto pkt_tb = pkt->time_base;
    ret = avcodec_send_packet(codec_ctx_, pkt);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to send packet to decoder, error: %d(%s)", ret, errbuf);
        return -1;
    }
    while(true) {
        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            LogErrorf(logger_, "Failed to alloc frame for decoder");
            return -1;
        }
        ret = avcodec_receive_frame(codec_ctx_, frame);
        if (ret < 0) {
            av_frame_free(&frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else {
                char errbuf[256];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LogErrorf(logger_, "Failed to receive frame from decoder, error: %d(%s)", ret, errbuf);
                return -1;
            }
        }
        if (frame->time_base.num == 0 || frame->time_base.den == 0) {
            frame->time_base = pkt_tb;
        }
        std::shared_ptr<FFmpegMediaPacket> out_pkt = std::make_shared<FFmpegMediaPacket>(frame, pkt_ptr->GetMediaPktType());
        if (!out_pkt) {
            av_frame_free(&frame);
            return -1;
        }

        if (sink_cb_) {
            out_pkt->SetId(id_);
            sink_cb_->OnData(out_pkt);
        } else {
            av_frame_free(&frame);
        }
    }
    return 0;
}

int Decoder::OpenDecoder(AVCodecID codec_id) {
    if (codec_ctx_) {
        return 0;
    }
    const AVCodec* codec = nullptr;
    if (codec_id == AV_CODEC_ID_OPUS) {
        codec = avcodec_find_decoder_by_name("libopus");
    } else {
        codec = avcodec_find_decoder(codec_id);
    }
    if (!codec) {
        LogErrorf(logger_, "Failed to find decoder for codec id %d", codec_id);
        return -1;
    }
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        LogErrorf(logger_, "Failed to alloc codec context for codec id %d", codec_id);
        return -1;
    }
    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to open codec id %d, error: %d(%s)", codec_id, ret, errbuf);
        avcodec_free_context(&codec_ctx_);
        return -1;
    }

    LogInfof(logger_, "Opened decoder by id for codec id %d(%s)", codec_id, avcodec_get_name(codec_id));
    return 0;
}

int Decoder::OpenDecoder(AVCodecParameters* params) {
    if (codec_ctx_) {
        return 0;
    }
    if (!params) {
        LogErrorf(logger_, "Invalid codec parameters");
        return -1;
    }
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        LogErrorf(logger_, "Failed to find decoder for codec id %d", params->codec_id);
        return -1;
    }
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        LogErrorf(logger_, "Failed to alloc codec context for codec id %d", params->codec_id);
        return -1;
    }
    int ret = avcodec_parameters_to_context(codec_ctx_, params);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to copy codec parameters to codec context, error: %d(%s)", ret, errbuf);
        avcodec_free_context(&codec_ctx_);
        return -1;
    }
    ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "Failed to open codec id %d, error: %d(%s)", params->codec_id, ret, errbuf);
        avcodec_free_context(&codec_ctx_);
        return -1;
    }

    LogInfof(logger_, "Opened decoder for codec id %d(%s)", params->codec_id, avcodec_get_name(params->codec_id));
    return 0;
}

void Decoder::CloseDecoder() {
    StopDecodeThread();

    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
}
