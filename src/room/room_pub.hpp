#ifndef ROOM_PUB_HPP_
#define ROOM_PUB_HPP_
#include "utils/logger.hpp"
#include "utils/data_buffer.hpp"
#include <memory>

namespace cpp_streamer {

class RoomNotificationInfo
{
public:
    RoomNotificationInfo() {}
    RoomNotificationInfo(
        const std::string& method,
        const std::string& room_id, 
        const std::string& user_id, 
        const std::string& msg) {
        this->method = method;
        this->room_id = room_id;
        this->user_id = user_id;
        this->msg = msg;
    }
    ~RoomNotificationInfo() {}
public:
    std::string method;
    std::string room_id;
    std::string user_id;
    std::string msg;
    int task_index = 0;
};

class RoomCallbackI
{
public:
    virtual void Notification2VoiceAgent(std::shared_ptr<RoomNotificationInfo> info_ptr) = 0;
};

}
#endif