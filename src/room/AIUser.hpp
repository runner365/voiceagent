#ifndef AI_USER_HPP_
#define AI_USER_HPP_
#include "utils/logger.hpp"
#include "tts/tts.hpp"
#include "transcode/pcm2opus.hpp"
#include <memory>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace cpp_streamer
{
class AIUser : public Pcm2OpusCallbackI
{
public:
    AIUser(const std::string& user_id, Pcm2OpusCallbackI* cb, Logger* logger);
    virtual ~AIUser();

public:
    const std::string& GetUserId() const { return user_id_; }

public:
    void InputText(const std::string& text);

public:
    virtual void OnOpusData(const std::vector<uint8_t>& opus_data, int sample_rate, int channels, int64_t pts, int task_index) override;

private:
    void OnTtsThread();
    void InsertTextIntoQueue(const std::string& text);
    std::string GetTextFromQueue();
    size_t GetTextQueueSize();

private:
    std::string user_id_;
    Pcm2OpusCallbackI* cb_ = nullptr;
    Logger* logger_;

private:
    std::unique_ptr<SherpaOnnxTTSImpl> tts_ptr_;

private:
    bool running_ = false;
    std::unique_ptr<std::thread> tts_thread_ptr_;
    std::mutex tts_mutex_;
    std::queue<std::string> text_queue_;
    std::condition_variable text_cv_;

private:
    std::unique_ptr<Pcm2Opus> pcm2opus_;
};

} // namespace cpp_streamer
#endif