#include "encoder.h"
#include "utils/uuid.hpp"

static const int kVIDEO_BASE_TIMES = 1000;

Encoder::Encoder(Logger* logger) {
    logger_ = logger;
    id_ = UUID::MakeUUID2();

    StartEncodeThread();
}

Encoder::~Encoder() {
    StopEncodeThread();
    CloseVideoEncoder();
	CloseAudioEncoder();
	ReleaseAudioFifo();
}

void Encoder::SetSinkCallback(SinkCallbackI* cb) {
    sink_cb_ = cb;
}

void Encoder::OnData(std::shared_ptr<FFmpegMediaPacket> pkt) {
    if (!thread_running_) {
        return;
    }
    if (pkt && pkt->IsAVFrame()) {
        InputFrame(pkt);
    }
}

int Encoder::InputFrame(std::shared_ptr<FFmpegMediaPacket> frame) {
    if (!frame || !frame->IsAVFrame()) {
        LogErrorf(logger_, "InputFrame() failed: invalid frame");
        return -1;
    }
    
    InsertFrameToQueue(frame);
    return 0;
}

int Encoder::OpenVideoEncoder(const VideoEncInfo& enc_info) {
    int ret = 0;

    if (video_codec_ctx_) {
        return 0; // Already opened
    }
    const AVCodec* codec = avcodec_find_encoder(enc_info.codec_id);
    if (!codec) {
        LogErrorf(logger_, "OpenVideoEncoder() failed: codec not found");
        return -1;
    }
    video_codec_ctx_ = avcodec_alloc_context3(codec);
    if (!video_codec_ctx_) {
        LogErrorf(logger_, "OpenVideoEncoder() failed: could not allocate codec context");
        return -1;
    }
    video_codec_ctx_->width = enc_info.width;
    video_codec_ctx_->height = enc_info.height;
    video_codec_ctx_->time_base = AVRational{1, enc_info.fps * kVIDEO_BASE_TIMES};
    video_codec_ctx_->framerate = AVRational{enc_info.fps, 1};
    video_codec_ctx_->bit_rate = enc_info.bitrate;
    video_codec_ctx_->pix_fmt = enc_info.pix_fmt;
    video_codec_ctx_->framerate = AVRational{enc_info.fps, 1};
    video_codec_ctx_->gop_size = enc_info.gop * enc_info.fps; // keyframe interval in frames
    video_codec_ctx_->thread_count = enc_info.threads > 0 ? enc_info.threads : 1;
	video_codec_ctx_->max_b_frames = 0; // no b-frames

    if (enc_info.codec_id == AV_CODEC_ID_H264) {
        if (enc_info.rc_mode == 0) { // CBR
            video_codec_ctx_->rc_max_rate = enc_info.bitrate;
            video_codec_ctx_->rc_buffer_size = enc_info.buf_size;
            video_codec_ctx_->bit_rate = enc_info.bitrate;
            video_codec_ctx_->flags |= AV_CODEC_FLAG_QSCALE;
            video_codec_ctx_->qmin = enc_info.qp;
            video_codec_ctx_->qmax = enc_info.qp;
        } else if (enc_info.rc_mode == 1) { // VBR
            video_codec_ctx_->rc_max_rate = enc_info.max_bitrate;
            video_codec_ctx_->rc_buffer_size = enc_info.buf_size;
            video_codec_ctx_->bit_rate = enc_info.bitrate;
        } else if (enc_info.rc_mode == 2) { // ABR
            video_codec_ctx_->bit_rate = enc_info.bitrate;
        }
        video_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        //set qp if needed
        if (enc_info.qp > 0) {
            video_codec_ctx_->flags |= AV_CODEC_FLAG_QSCALE;
            video_codec_ctx_->qmin = enc_info.qp;
            video_codec_ctx_->qmax = enc_info.qp;
        }
        //set vbv if needed
        if (enc_info.vbv_maxrate > 0 && enc_info.vbv_bufsize > 0) {
            video_codec_ctx_->rc_max_rate = enc_info.vbv_maxrate;
            video_codec_ctx_->rc_buffer_size = enc_info.vbv_bufsize;
        }

        LogInfof(logger_, "set h264 profile:%s, tune:%s, preset:%s, fps:%d", 
            enc_info.profile.c_str(), enc_info.tune.c_str(), enc_info.preset.c_str(), enc_info.fps);
        if ((ret = av_opt_set(video_codec_ctx_->priv_data, "profile", enc_info.profile.c_str(), 0)) < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
			LogErrorf(logger_, "fail to set profile: %s", errbuf);
            return ret;
        }

        if ((ret = av_opt_set(video_codec_ctx_->priv_data, "tune", enc_info.tune.c_str(), 0)) < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LogErrorf(logger_, "fail to set tune: %s", errbuf);
            return ret;
        }

        if ((ret = av_opt_set(video_codec_ctx_->priv_data, "preset", enc_info.preset.c_str(), 0)) < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LogErrorf(logger_, "fail to set preset: %s", errbuf);
            return ret;
        }
    }

    if (avcodec_open2(video_codec_ctx_, codec, nullptr) < 0) {
        LogErrorf(logger_, "OpenVideoEncoder() failed: could not open codec");
        avcodec_free_context(&video_codec_ctx_);
        video_codec_ctx_ = nullptr;
        return -1;
    }
    std::string dump = DumpVideoEncInfo(enc_info);
    LogInfof(logger_, "Video encoder opened: %s", dump.c_str());
    return 0;
}

int Encoder::OpenAudioEncoder(const AudioEncInfo& enc_info, const char* codec_name) {
    if (audio_codec_ctx_) {
        return 0; // Already opened
    }
    const AVCodec* codec;
    if (codec_name) {
        codec = avcodec_find_encoder_by_name(codec_name);
    } else {
        codec = avcodec_find_encoder(enc_info.codec_id);
    }
    if (!codec) {
        LogErrorf(logger_, "OpenAudioEncoder() failed: codec not found");
        return -1;
    }
    audio_codec_ctx_ = avcodec_alloc_context3(codec);
    if (!audio_codec_ctx_) {
        LogErrorf(logger_, "OpenAudioEncoder() failed: could not allocate codec context");
        return -1;
    }
    audio_codec_ctx_->sample_rate = enc_info.sample_rate;
    audio_codec_ctx_->ch_layout.nb_channels = enc_info.channels;
    audio_codec_ctx_->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
    audio_codec_ctx_->ch_layout.u.mask = (1ULL << enc_info.channels) - 1;
    audio_codec_ctx_->bit_rate = enc_info.bitrate;
    audio_codec_ctx_->sample_fmt = enc_info.sample_fmt;
    audio_codec_ctx_->time_base = AVRational{1, enc_info.sample_rate};
    audio_codec_ctx_->frame_size = enc_info.frame_size > 0 ? enc_info.frame_size : 2048;

    if (avcodec_open2(audio_codec_ctx_, codec, nullptr) < 0) {
        LogErrorf(logger_, "OpenAudioEncoder() failed: could not open codec");
        avcodec_free_context(&audio_codec_ctx_);
        audio_codec_ctx_ = nullptr;
        return -1;
    }
    std::string dump = DumpAudioEncInfo(enc_info);
    LogInfof(logger_, "Audio encoder opened: %s", dump.c_str());
    return 0;
}

void Encoder::CloseVideoEncoder() {
    if (video_codec_ctx_) {
        avcodec_free_context(&video_codec_ctx_);
        video_codec_ctx_ = nullptr;
    }
}

void Encoder::CloseAudioEncoder() {
    if (audio_codec_ctx_) {
        avcodec_free_context(&audio_codec_ctx_);
        audio_codec_ctx_ = nullptr;
    }
}

int Encoder::InsertFrameToQueue(std::shared_ptr<FFmpegMediaPacket> frame) {
    std::lock_guard<std::mutex> lock(frame_mutex_);

    frame_queue_.push(frame);
    frame_cond_.notify_one();
    return 0;
}

std::shared_ptr<FFmpegMediaPacket> Encoder::GetFrameFromQueue() {
    std::unique_lock<std::mutex> lock(frame_mutex_);
    //wait until there is a frame or thread is stopping or timeout 2 seconds
    frame_cond_.wait_for(lock, std::chrono::seconds(2), [this]() { return !frame_queue_.empty() || !thread_running_; });
    if (!frame_queue_.empty()) {
        auto frame = frame_queue_.front();
        frame_queue_.pop();
        return frame;
    }
    return nullptr;
}

size_t Encoder::GetFrameQueueSize() {
	std::unique_lock<std::mutex> lock(frame_mutex_);
	return frame_queue_.size();
}

void Encoder::StartEncodeThread() {
    if (thread_running_) {
        return;
    }
    thread_running_ = true;
    encode_thread_ptr_ = std::make_unique<std::thread>(&Encoder::EncodeThread, this);
}

void Encoder::StopEncodeThread() {
    if (!thread_running_) {
        return;
    }
    thread_running_ = false;
    if (encode_thread_ptr_ && encode_thread_ptr_->joinable()) {
        frame_cond_.notify_all();
        encode_thread_ptr_->join();
    }
    encode_thread_ptr_.reset();
}

void Encoder::EncodeThread() {
    LogInfof(logger_, "Encoder thread started");
    while (thread_running_) {
        auto frame = GetFrameFromQueue();
        if (!frame) {
            if (!thread_running_) {
                break;
		    }
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!thread_running_) {
            break;
		}

        if (frame->GetMediaPktType() == MEDIA_AUDIO_TYPE) {
            int64_t pts_ms = frame->GetAVFrame() ? av_rescale_q(frame->GetAVFrame()->pts, frame->GetAVFrame()->time_base, AVRational { 1, 1000 }) : -1;

            LogDebugf(logger_, "Encoding audio frame, pts_ms:%lld, queue size:%zu", pts_ms, GetFrameQueueSize());
            int ret = HandleAudioEncodedPacket(frame);
            if (ret < 0) {
                LogErrorf(logger_, "EncodeThread() failed: HandleAudioEncodedPacket error");
            }
            continue;
        }
        else if (frame->GetMediaPktType() == MEDIA_VIDEO_TYPE) {
			int64_t pts_ms = frame->GetAVFrame() ? av_rescale_q(frame->GetAVFrame()->pts, frame->GetAVFrame()->time_base, AVRational { 1, 1000 }) : -1;

			LogDebugf(logger_, "Encoding video frame, pts_ms:%lld, queue size:%zu", pts_ms, GetFrameQueueSize());
            int ret = HandleVideoEncodedPacket(frame);
            if (ret < 0) {
                LogErrorf(logger_, "EncodeThread() failed: HandleVideoEncodedPacket error");
            }
            continue;
        }
        else {
            LogErrorf(logger_, "EncodeThread() failed: unknown media type");
            continue;
        }
    }

    FlushVideoFrame();
	FlushAudioFrame();

    LogInfof(logger_, "Encoder thread stopped");
}

int Encoder::HandleAudioEncodedPacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr) {
    if (!pkt_ptr || !pkt_ptr->IsAVFrame()) {
        LogErrorf(logger_, "HandleEncodedPacket() failed: invalid avframe");
        return -1;
	}

    MEDIA_PKT_TYPE pkt_type = pkt_ptr->GetMediaPktType();
    AVFrame* in_frame = pkt_ptr->GetAVFrame();
    if (!audio_codec_ctx_) {
        LogErrorf(logger_, "HandleEncodedPacket() failed: codec context not opened");
        return -1;
    }
    if (in_frame->sample_rate != audio_codec_ctx_->sample_rate ||
        in_frame->ch_layout.nb_channels != audio_codec_ctx_->ch_layout.nb_channels ||
        in_frame->format != audio_codec_ctx_->sample_fmt) {
        char errbuf[256];
        av_strerror(AVERROR(EINVAL), errbuf, sizeof(errbuf));
        LogErrorf(logger_, "HandleEncodedPacket() failed: input frame parameters do not match encoder context, error:%s", errbuf);
        LogErrorf(logger_, "input frame sample_rate:%d, channels:%d, format:%d, codec rate:%d, channels:%d, sample_fmt:%d", 
            in_frame->sample_rate, in_frame->ch_layout.nb_channels, in_frame->format, 
            audio_codec_ctx_->sample_rate, audio_codec_ctx_->ch_layout.nb_channels, audio_codec_ctx_->sample_fmt);
        return -1;
    }
	AVFrame* frame = av_frame_clone(in_frame);


	// frame->linesize[0] = frame->nb_samples * av_get_bytes_per_sample((AVSampleFormat)frame->format) * audio_codec_ctx_->ch_layout.nb_channels;
    
    int ret = InitAudioFifo();
    if (ret < 0) {
        LogErrorf(logger_, "HandleEncodedPacket() failed: InitAudioFifo error");
        return -1;
	}
	ret = AddSamplesToFifo(frame);
    if (ret < 0) {
        LogErrorf(logger_, "HandleEncodedPacket() failed: AddSamplesToFifo error");
        return -1;
    }
	std::vector<AVFrame*> frames;
	size_t frame_count = GetSamplesFromFifo(frame, frames);
    if (frame_count == 0) {
        return 0; // Not enough samples to encode
    }

    for (AVFrame* input_frame : frames) {
        if (frame->time_base.den > 0 && frame->time_base.num > 0) {
            input_frame->pts = av_rescale_q(input_frame->pts, frame->time_base, audio_codec_ctx_->time_base);
        }
        ret = avcodec_send_frame(audio_codec_ctx_, input_frame);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LogErrorf(logger_, "HandleEncodedPacket() failed: could not send frame to codec, error:%s", errbuf);
            av_frame_free(&input_frame);
            continue;
        }
        av_frame_free(&input_frame);
        while (true) {
            AVPacket* pkt = av_packet_alloc();
            if (!pkt) {
                LogErrorf(logger_, "HandleEncodedPacket() failed: could not allocate packet");
                return -1;
            }
            ret = avcodec_receive_packet(audio_codec_ctx_, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                // No more packets to receive
                av_packet_free(&pkt);
                break;
            } else if (ret < 0) {
                LogErrorf(logger_, "HandleEncodedPacket() failed: could not receive packet from codec");
                av_packet_free(&pkt);
                return -1;
            }
            if (audio_codec_ctx_->time_base.den > 0 && audio_codec_ctx_->time_base.num > 0) {
				pkt->time_base = audio_codec_ctx_->time_base;
			}
            std::shared_ptr<FFmpegMediaPacket> pkt_ptr = std::make_shared<FFmpegMediaPacket>(pkt, pkt_type);
			std::string dump = pkt_ptr->Dump();
            LogDebugf(logger_, "Audio Encoded packet: %s", dump.c_str());
            // Process the encoded packet
			if (sink_cb_) {
                FFmpegMediaPacketPrivate prv;

                prv.private_type_ = PRIVATE_DATA_TYPE_AUDIO_ENC;
                prv.private_data_ = audio_codec_ctx_;
                prv.private_data_owner_ = false;
                pkt_ptr->SetPrivateData(prv);
                pkt_ptr->SetId(id_);
                sink_cb_->OnData(pkt_ptr);
            }
        }
	}
    return 0;
}

int Encoder::DoVideoEncode(AVFrame* frame) {

    if (first_video_frame_) {
        first_video_frame_ = false;
		frame->pict_type = AV_PICTURE_TYPE_I; // Force first frame as keyframe
    } else {
        frame->pict_type = AV_PICTURE_TYPE_NONE; // Let encoder decide
	}
    int ret = avcodec_send_frame(video_codec_ctx_, frame);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "HandleEncodedPacket() failed: could not send frame to codec, error:%s", errbuf);
        return -1;
    }

    while (true) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            LogErrorf(logger_, "HandleEncodedPacket() failed: could not allocate packet");
            return -1;
        }
        ret = avcodec_receive_packet(video_codec_ctx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more packets to receive
            break;
        } else if (ret < 0) {
            LogErrorf(logger_, "HandleEncodedPacket() failed: could not receive packet from codec");
            return -1;
        }

        if (video_codec_ctx_->time_base.den > 0 && video_codec_ctx_->time_base.num > 0) {
            pkt->time_base = video_codec_ctx_->time_base;
		}
		std::shared_ptr<FFmpegMediaPacket> pkt_ptr = std::make_shared<FFmpegMediaPacket>(pkt, MEDIA_VIDEO_TYPE);
        std::string dump = pkt_ptr->Dump();
        LogDebugf(logger_, "Video Encoded packet: %s", dump.c_str());

        if (pkt->dts <= last_video_dts_) {
            LogWarnf(logger_, "video encode non monotonically increasing dts, pkt dts:%lld, last dts:%lld",
                pkt->dts, last_video_dts_);
        }
        last_video_dts_ = pkt->dts;
		if (sink_cb_) {
            FFmpegMediaPacketPrivate prv;

            prv.private_type_ = PRIVATE_DATA_TYPE_VIDEO_ENC;
            prv.private_data_ = video_codec_ctx_;
            prv.private_data_owner_ = false;
            pkt_ptr->SetPrivateData(prv);
            pkt_ptr->SetId(id_);
            sink_cb_->OnData(pkt_ptr);
        }
    }
    return 0;
}

int Encoder::HandleVideoEncodedPacket(std::shared_ptr<FFmpegMediaPacket> pkt_ptr) {
    int ret = 0;

    if (!pkt_ptr || !pkt_ptr->IsAVFrame()) {
        LogErrorf(logger_, "HandleEncodedPacket() failed: invalid avframe");
        return -1;
    }
	// MEDIA_PKT_TYPE pkt_type = pkt_ptr->GetMediaPktType();
    AVFrame* frame = pkt_ptr->GetAVFrame();
    if (!video_codec_ctx_) {
        LogErrorf(logger_, "HandleEncodedPacket() failed: codec context not opened");
        return -1;
    }
	int64_t old_pts = frame->pts;
    if (frame->time_base.den > 0 && frame->time_base.num > 0) {
        frame->pts = av_rescale_q(frame->pts, frame->time_base, video_codec_ctx_->time_base);
    }
    int64_t expected_pts = last_vframe_pts_ + kVIDEO_BASE_TIMES;

	LogDebugf(logger_, "Video frame pts rescale from %lld to %lld, expected pts:%lld", old_pts, frame->pts, expected_pts);
    if (last_vframe_pts_ < 0) {
        last_vframe_pts_ = frame->pts;
        ret = DoVideoEncode(frame);
        if (ret < 0) {
            LogErrorf(logger_, "HandleVideoEncodedPacket() failed: DoVideoEncode error");
            return -1;
        }
    } else {
        if ((frame->pts > expected_pts) && (frame->pts - expected_pts > (kVIDEO_BASE_TIMES/10))) {
            // it means frame->pts > expected_pts when input fps is less than encoder fps
            const int max_insert_frames = 10;
            int index = 0;
            // Need to insert dummy frames
            while (expected_pts < frame->pts) {
                index++;
                if (index > max_insert_frames) {
                    LogWarnf(logger_, "too many missing frames, pts jump from %lld to %lld, max insert %d frames",
                        last_vframe_pts_, frame->pts, max_insert_frames);
                    break;
                }
                AVFrame* dummy_frame = av_frame_clone(frame);
                if (!dummy_frame) {
                    LogErrorf(logger_, "HandleVideoEncodedPacket() failed: could not clone frame for dummy");
                    return -1;
                }
                dummy_frame->pts = expected_pts;
				LogDebugf(logger_, "insert dummy video frame, pts:%lld", dummy_frame->pts);
                ret = DoVideoEncode(dummy_frame);
                av_frame_free(&dummy_frame);
                if (ret < 0) {
                    LogErrorf(logger_, "HandleVideoEncodedPacket() failed: DoVideoEncode error for dummy");
                    return -1;
                }
                last_vframe_pts_ = expected_pts;
                expected_pts += kVIDEO_BASE_TIMES;
            }
        } else if ((frame->pts < expected_pts) && (expected_pts - frame->pts > (kVIDEO_BASE_TIMES / 10))) {
            // it means frame->pts < expected_pts when input fps is higher than encoder fps
            LogDebugf(logger_, "drop video frame, pts:%lld, expected pts:%lld", frame->pts, expected_pts);
        } else {
            // normal case, pts == expected_pts
            LogDebugf(logger_, "normal video frame, pts:%lld", frame->pts);
            ret = DoVideoEncode(frame);
            if (ret < 0) {
                LogErrorf(logger_, "HandleVideoEncodedPacket() failed: DoVideoEncode error");
                return -1;
            }
            last_vframe_pts_ = frame->pts;
        }
    }

    return ret;
}

void Encoder::FlushVideoFrame() {
    if (!video_codec_ctx_) {
        return;
    }
	LogInfof(logger_, "Flushing video encoder");
    int ret = avcodec_send_frame(video_codec_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "FlushVideoFrame() failed: could not send frame to codec, error:%s", errbuf);
        return;
    }

    int index = 0;
    while (true) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            LogErrorf(logger_, "FlushVideoFrame() failed: could not allocate packet");
            return;
        }
        ret = avcodec_receive_packet(video_codec_ctx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more packets to receive
            av_packet_free(&pkt);
            break;
        } else if (ret < 0) {
            LogErrorf(logger_, "FlushVideoFrame() failed: could not receive packet from codec");
            av_packet_free(&pkt);
            return;
        }
        if (video_codec_ctx_->time_base.den > 0 && video_codec_ctx_->time_base.num > 0) {
            pkt->time_base = video_codec_ctx_->time_base;
        }
		std::shared_ptr<FFmpegMediaPacket> pkt_ptr = std::make_shared<FFmpegMediaPacket>(pkt, MEDIA_VIDEO_TYPE);
        std::string dump = pkt_ptr->Dump();
        LogInfof(logger_, "Video left Encoded packet: %s, index: %d", dump.c_str(), ++index);
        // Process the encoded packet
        if (sink_cb_) {
            pkt_ptr->SetId(id_);
            sink_cb_->OnData(pkt_ptr);
        }
    }
}

void Encoder::FlushAudioFrame() {
    if (!audio_codec_ctx_) {
        return;
	}
	LogInfof(logger_, "Flushing audio encoder");
    int ret = avcodec_send_frame(audio_codec_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "FlushAudioFrame() failed: could not send frame to codec, error:%s", errbuf);
        return;
    }
    int index = 0;
    while (true) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            LogErrorf(logger_, "FlushAudioFrame() failed: could not allocate packet");
            return;
        }
        ret = avcodec_receive_packet(audio_codec_ctx_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more packets to receive
            av_packet_free(&pkt);
            break;
        } else if (ret < 0) {
            LogErrorf(logger_, "FlushAudioFrame() failed: could not receive packet from codec");
            av_packet_free(&pkt);
            return;
        }
        if (audio_codec_ctx_->time_base.den > 0 && audio_codec_ctx_->time_base.num > 0) {
            pkt->time_base = audio_codec_ctx_->time_base;
        }
        std::shared_ptr<FFmpegMediaPacket> pkt_ptr = std::make_shared<FFmpegMediaPacket>(pkt, MEDIA_AUDIO_TYPE);
        std::string dump = pkt_ptr->Dump();
        LogInfof(logger_, "Audio left Encoded packet: %s, index: %d", dump.c_str(), ++index);
        // Process the encoded packet
        if (sink_cb_) {
            pkt_ptr->SetId(id_);
            sink_cb_->OnData(pkt_ptr);
        }
	}
}

int Encoder::InitAudioFifo() {
    if (audio_fifo_) {
        return 0; // Already initialized
	}
    if (!audio_codec_ctx_) {
		LogErrorf(logger_, "InitAudioFifo() failed: codec context not opened");
        return -1;
	}
    const int NB_SAMPLES = audio_codec_ctx_->frame_size;
	audio_fifo_ = av_audio_fifo_alloc(audio_codec_ctx_->sample_fmt, audio_codec_ctx_->ch_layout.nb_channels, NB_SAMPLES);
    if (!audio_fifo_) {
        LogErrorf(logger_, "InitAudioFifo() failed: could not allocate audio fifo");
        return -1;
	}
	LogInfof(logger_, "Audio fifo initialized");
    return 0;
}

void Encoder::ReleaseAudioFifo() {
    if (!audio_fifo_) {
        return;
	}
    av_audio_fifo_free(audio_fifo_);
	audio_fifo_ = nullptr;
	LogInfof(logger_, "Audio fifo released");
}

int Encoder::AddSamplesToFifo(AVFrame* frame) {
	int err = av_audio_fifo_realloc(audio_fifo_, av_audio_fifo_size(audio_fifo_) + frame->nb_samples);
    if (err < 0) {
        char errbuf[256];
        av_strerror(err, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "AddSamplesToFifo() failed: could not reallocate audio fifo, error:%s", errbuf);
        return -1;
	}

	err = av_audio_fifo_write(audio_fifo_, (void**)frame->data, frame->nb_samples);
    if (err < frame->nb_samples) {
        char errbuf[256];
        av_strerror(err, errbuf, sizeof(errbuf));
        LogErrorf(logger_, "AddSamplesToFifo() failed: could not write data to audio fifo, error:%s", errbuf);
        return -1;
	}
	return 0;
}

size_t Encoder::GetSamplesFromFifo(AVFrame* input_frame, std::vector<AVFrame*>& frames) {

    while (true) {
        int fifo_size = av_audio_fifo_size(audio_fifo_);

        if (fifo_size < audio_codec_ctx_->frame_size) {
            return frames.size();
        }
        uint8_t* frame_buf = nullptr;
        AVFrame* dst_frame_p = GetNewAudioFrame(frame_buf);
        av_audio_fifo_read(audio_fifo_, (void**)dst_frame_p->data, audio_codec_ctx_->frame_size);
        av_frame_copy_props(dst_frame_p, input_frame);
        dst_frame_p->nb_samples = audio_codec_ctx_->frame_size;
        dst_frame_p->ch_layout = audio_codec_ctx_->ch_layout;
        dst_frame_p->format = audio_codec_ctx_->sample_fmt;
        dst_frame_p->pkt_dts = input_frame->pkt_dts;
        dst_frame_p->pts = input_frame->pts;
        if (last_audio_pts_ >= input_frame->pts) {
            dst_frame_p->pts = last_audio_pts_ + audio_codec_ctx_->frame_size;
        }
        last_audio_pts_ = dst_frame_p->pts;
        dst_frame_p->pts = av_rescale_q(dst_frame_p->pts, input_frame->time_base, audio_codec_ctx_->time_base);
        dst_frame_p->pict_type = AV_PICTURE_TYPE_NONE;

		frames.push_back(dst_frame_p);
    }

    return frames.size();
}

AVFrame* Encoder::GetNewAudioFrame(uint8_t*& frame_buf) {
    AVFrame* ret_frame = av_frame_alloc();

    ret_frame->nb_samples = audio_codec_ctx_->frame_size;
    ret_frame->format = audio_codec_ctx_->sample_fmt;
    ret_frame->ch_layout = audio_codec_ctx_->ch_layout;
    ret_frame->sample_rate = audio_codec_ctx_->sample_rate;
    int size = av_samples_get_buffer_size(NULL, audio_codec_ctx_->ch_layout.nb_channels, audio_codec_ctx_->frame_size, audio_codec_ctx_->sample_fmt, 0);

    frame_buf = (uint8_t*)av_malloc(size);

    avcodec_fill_audio_frame(ret_frame, audio_codec_ctx_->ch_layout.nb_channels,
        audio_codec_ctx_->sample_fmt, (const uint8_t*)frame_buf, size, 1);

    return ret_frame;
}