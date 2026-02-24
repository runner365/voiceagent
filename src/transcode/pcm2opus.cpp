#include "pcm2opus.hpp"

namespace cpp_streamer
{
bool GenAvFramesFromPcmFloatData(const std::vector<float>& pcm_float_data, 
    int sample_rate, int channels, int duration_ms, std::vector<AVFrame*>& out_frames, int64_t& next_pts, Logger* logger) {
    int num_samples_per_frame = (sample_rate * duration_ms) / 1000;
    size_t total_samples = pcm_float_data.size() / channels;
    if (total_samples < num_samples_per_frame) {
        return false;
    }

    size_t num_frames = total_samples / num_samples_per_frame;
    for (size_t i = 0; i < num_frames; ++i) {
        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            for (auto& f : out_frames) {
                av_frame_free(&f);
            }
            return false;
        }
        frame->nb_samples = num_samples_per_frame;
        frame->format = AV_SAMPLE_FMT_FLT;
        av_channel_layout_default(&frame->ch_layout, channels);
        frame->sample_rate = sample_rate;
        next_pts += num_samples_per_frame;
        frame->pts = next_pts; // in sample rate units

        int ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LogErrorf(logger, "av_frame_get_buffer failed: %s", errbuf);
            av_frame_free(&frame);
            continue;
        }
        // Copy PCM data to frame
        for (int ch = 0; ch < channels; ++ch) {
            memcpy(frame->data[ch], &pcm_float_data[(i * num_samples_per_frame * channels) + ch], num_samples_per_frame * sizeof(float));
            frame->linesize[ch] = num_samples_per_frame * sizeof(float);
        }
        out_frames.push_back(frame);
    }

    return true;
}

Pcm2Opus::Pcm2Opus(Pcm2OpusCallbackI* cb, Logger* logger)
{
    cb_ = cb;
    logger_ = logger;
    LogInfof(logger_, "Pcm2Opus constructed");
}

Pcm2Opus::~Pcm2Opus()
{
    LogInfof(logger_, "Pcm2Opus destructed");
    StopWorkerThread();
}

void Pcm2Opus::InsertPcmData(const PCM_DATA_INFO& pcm_data) {

    StartWorkerThread();
    AddPcmQueue(pcm_data);
}

void Pcm2Opus::AddPcmQueue(const PCM_DATA_INFO& pcm_data) {
    std::lock_guard<std::mutex> lock(pcm_data_mutex_);
    pcm_data_queue_.push(pcm_data);
    pcm_data_cv_.notify_one();
}

PCM_DATA_INFO Pcm2Opus::GetPcmFromQueue() {
    std::unique_lock<std::mutex> lock(pcm_data_mutex_);
    while (pcm_data_queue_.empty() && encode_thread_running_) {
        pcm_data_cv_.wait(lock);
    }
    PCM_DATA_INFO pcm_data;
    if (!encode_thread_running_) {
        return pcm_data;
    }
    
    if (!pcm_data_queue_.empty()) {
        pcm_data = pcm_data_queue_.front();
        pcm_data_queue_.pop();
    }
    return pcm_data;
}

size_t Pcm2Opus::GetPcmQueueSize() {
    std::lock_guard<std::mutex> lock(pcm_data_mutex_);
    return pcm_data_queue_.size();
}

void Pcm2Opus::OnData(std::shared_ptr<FFmpegMediaPacket> pkt_ptr) {
    if (!pkt_ptr) {
        return;
    }
    if (!encode_thread_running_) {
        return;
    }
    if (pcm_filter_ != nullptr && pkt_ptr->GetId() == pcm_filter_->GetId()) {
        AVFrame* frame = pkt_ptr->GetAVFrame();
        if (!frame) {
            return;
        }
        HandleFrameInEncoder(frame);
        return;
    }

    if (opus_encoder_ != nullptr && pkt_ptr->GetId() == opus_encoder_->GetId()) {
        //output from encoder, can be sent to remote or saved to file
        if (cb_) {
            AVPacket* pkt = pkt_ptr->GetAVPacket();
            if (pkt) {
                std::vector<uint8_t> opus_data(pkt->data, pkt->data + pkt->size);
                cb_->OnOpusData(opus_data, 48000, 2, pkt->pts, current_index_);
            }
        }
        return;
    }
    LogErrorf(logger_, "Pcm2Opus OnData unknown pkt id:%s", pkt_ptr->GetId().c_str());
}

void Pcm2Opus::OnWorkerThread() {
    LogInfof(logger_, "Pcm2Opus worker thread is running");
    while (encode_thread_running_) {
        PCM_DATA_INFO pcm_data = GetPcmFromQueue();
        if (pcm_data.pcm_float_data.empty()) {
            continue;
        }
        LogInfof(logger_, "Pcm2Opus OnWorkerThread processing pcm data, sample_rate:%d, channels:%d, data_size:%zu, queue_size:%zu", 
            pcm_data.sample_rate, pcm_data.channels, pcm_data.pcm_float_data.size(), GetPcmQueueSize());
        std::vector<AVFrame*> pcm_frames_;

        bool r = GenAvFramesFromPcmFloatData(pcm_data.pcm_float_data, pcm_data.sample_rate, pcm_data.channels, 20, pcm_frames_, next_audio_pts_, logger_);
        if (!r) {
            LogErrorf(logger_, "Pcm2Opus OnWorkerThread GenAvFramesFromPcmFloatData failed");
            continue;
        }
        current_index_++;
        for (auto& frame : pcm_frames_) {
            HandleFrameInFilter(frame);
        }
    }
}

void Pcm2Opus::HandleFrameInFilter(AVFrame* frame) {
    if (!pcm_filter_) {
        pcm_filter_ = std::make_unique<MediaFilter>(logger_);
        //init param
        AudioFilter::Params input_param = {
            .sample_rate = frame->sample_rate,
            .ch_layout = frame->ch_layout,
            .sample_fmt = (AVSampleFormat)frame->format,
            .time_base = {1, frame->sample_rate},
        };
        const std::string filter_desc = "aresample=48000,aformat=sample_fmts=s16:channel_layouts=stereo";
        pcm_filter_->SetSinkCallback(this);
        //output is opus, rate 48000, channel=2, s16 format
        int ret = pcm_filter_->InitAudioFilter(input_param, filter_desc.c_str());
        if (ret != 0) {
            LogErrorf(logger_, "Pcm2Opus InitAudioFilter failed, ret:%d", ret);
            pcm_filter_.reset();
            av_frame_free(&frame);
            return;
        }
        
        LogInfof(logger_, "Pcm2Opus MediaFilter initialized with filter_desc:%s, input params rate:%d, channels:%d", filter_desc.c_str(), frame->sample_rate, frame->ch_layout.nb_channels);
    }

    //input frame to filter
    if (pcm_filter_) {
        pcm_filter_->OnData(std::make_shared<FFmpegMediaPacket>(frame, MEDIA_AUDIO_TYPE));
    }
}

void Pcm2Opus::HandleFrameInEncoder(AVFrame* frame) {
    if (!opus_encoder_) {
        opus_encoder_ = std::make_unique<Encoder>(logger_);
        //init param
        AudioEncInfo enc_info = {
            .codec_id = AV_CODEC_ID_OPUS,
            .sample_rate = 48000,
            .channels = 2,
            .bitrate = 32*1000, //bps
            .sample_fmt = AV_SAMPLE_FMT_S16,
            .frame_size = 960, //20ms for opus
        };
        int ret = opus_encoder_->OpenAudioEncoder(enc_info, "libopus");
        if (ret != 0) {
            opus_encoder_.reset();
            av_frame_free(&frame);
            return;
        }
        opus_encoder_->SetSinkCallback(this);
    }

    //input frame to encoder
    if (opus_encoder_) {
        AVFrame* in_frame = av_frame_clone(frame);
        opus_encoder_->OnData(std::make_shared<FFmpegMediaPacket>(in_frame, MEDIA_AUDIO_TYPE));
    }
}
void Pcm2Opus::StartWorkerThread() {
    if (encode_thread_running_) {
        return;
    }
    encode_thread_running_ = true;
    LogInfof(logger_, "Pcm2Opus worker thread started");
    encode_thread_ptr_.reset(new std::thread(&Pcm2Opus::OnWorkerThread, this));
}

void Pcm2Opus::StopWorkerThread() {
    if (!encode_thread_running_) {
        return;
    }
    encode_thread_running_ = false;
    LogInfof(logger_, "Pcm2Opus worker thread stopped");
    pcm_data_cv_.notify_all();
    encode_thread_ptr_->join();
    encode_thread_ptr_.reset(nullptr);

}

}