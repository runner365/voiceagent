#include "AIUser.hpp"

namespace cpp_streamer
{
AIUser::AIUser(const std::string& user_id, Pcm2OpusCallbackI* cb, Logger* logger)
    : user_id_(user_id), cb_(cb), logger_(logger) {
    tts_ptr_ = std::make_unique<SherpaOnnxTTSImpl>(logger_);

    LogInfof(logger_, "AIUser %s created", user_id_.c_str());
    running_ = true;
    tts_thread_ptr_ = std::make_unique<std::thread>(&AIUser::OnTtsThread, this);
}

AIUser::~AIUser() {
    LogInfof(logger_, "AIUser %s destroyed", user_id_.c_str());
    running_ = false;
    if (tts_thread_ptr_) {
        tts_thread_ptr_->join();
        tts_thread_ptr_.reset();
    }
    if (tts_ptr_) {
        tts_ptr_->Release();
        tts_ptr_.reset();
    }
}

void AIUser::InsertTextIntoQueue(const std::string& text) {
    std::unique_lock<std::mutex> lock(tts_mutex_);
    text_queue_.push(text);
    text_cv_.notify_one();
}

std::string AIUser::GetTextFromQueue() {
    std::unique_lock<std::mutex> lock(tts_mutex_);
    //judge if queue is empty and running_ is true
    text_cv_.wait(lock, [this] { return !text_queue_.empty() || !running_; });
    if (!running_) {
        return "";
    }
    std::string text = text_queue_.front();
    text_queue_.pop();
    return text;
}

size_t AIUser::GetTextQueueSize() {
    std::unique_lock<std::mutex> lock(tts_mutex_);
    return text_queue_.size();
}


void AIUser::InputText(const std::string& text) {
    InsertTextIntoQueue(text);
}

void AIUser::OnTtsThread() {
    LogInfof(logger_, "AIUser %s tts thread started", user_id_.c_str());
    while(running_) {
        std::string text = GetTextFromQueue();
        if (text.empty()) {
            continue;
        }
        LogInfof(logger_, "AIUser %s tts thread input text: %s", user_id_.c_str(), text.c_str());
        if (tts_ptr_) {
            int r = tts_ptr_->Init();
            if (r != 0) {
                LogErrorf(logger_, "Init tts failed, ret: %d", r);
                return;
            }
            int32_t sample_rate = 0;
            
            std::vector<float> audio_data;
            r = tts_ptr_->SynthesizeText(text, sample_rate, audio_data);
            if (r != 0) {
                LogErrorf(logger_, "SynthesizeText failed, ret: %d", r);
                continue;
            }
            if (audio_data.empty()) {
                LogErrorf(logger_, "SynthesizeText failed, audio_data empty");
                continue;
            }
            if (sample_rate == 0) {
                LogErrorf(logger_, "SynthesizeText failed, sample_rate is 0");
                continue;
            }
            if (!pcm2opus_) {
                pcm2opus_.reset(new Pcm2Opus(this, logger_));
            }
            PCM_DATA_INFO pcm_data_info(audio_data, sample_rate, 1);
            pcm2opus_->InsertPcmData(pcm_data_info);
        }
    }
}

void AIUser::OnOpusData(const std::vector<uint8_t>& opus_data, int sample_rate, int channels, int64_t pts, int task_index) {
    if (cb_) {
        cb_->OnOpusData(opus_data, sample_rate, channels, pts, task_index);
    }
}

} // namespace cpp_streamer