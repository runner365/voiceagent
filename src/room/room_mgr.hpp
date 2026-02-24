#ifndef ROOM_MGR_HPP_
#define ROOM_MGR_HPP_
#include "utils/timer.hpp"
#include "utils/logger.hpp"
#include "utils/json.hpp"
#include "ws_message/ws_protoo_info.hpp"
#include "ws_message/ws_protoo_client.hpp"
#include "room_pub.hpp"
#include <uv.h>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace cpp_streamer {

class Room;
class RoomMgr : public TimerInterface, 
                public WsProtooClientCallbackI, 
                public RoomCallbackI
{
public:
    virtual ~RoomMgr();

public:
    static int Initialize(uv_loop_t* loop, Logger* logger);
    static RoomMgr* Instance();

public:
    virtual void OnConnected() override;
    virtual void OnResponse(const std::string& text) override;
    virtual void OnNotification(const std::string& text) override;
    virtual void OnClosed(int code, const std::string& reason) override;

public:
    virtual void Notification2VoiceAgent(std::shared_ptr<RoomNotificationInfo> info_ptr) override;

protected:
    virtual bool OnTimer() override;

protected:
    RoomMgr(uv_loop_t* loop, Logger* logger);

private:
    int Connect();
    void EchoRequest();
    void OnSendPcmData2VoiceAgent();
    void OnCheckRoomAlive();

private:
    void OnHandleOpusData(const nlohmann::json& j);
    void OnHandleResponseText(const nlohmann::json& j);

private:
    std::shared_ptr<Room> GetorCreateRoom(const std::string& room_id);
    void EraseRoom(const std::string& room_id);

private:
    void InsertRoomNotification(std::shared_ptr<RoomNotificationInfo> info_ptr);
    bool PopRoomNotification(std::vector<std::shared_ptr<RoomNotificationInfo>>& info_vec);
    size_t GetRoomNotificationSize();

private:
    static RoomMgr* instance_;

private:
    uv_loop_t* loop_ = nullptr;
    Logger* logger_ = nullptr;

private:
    std::unique_ptr<WsProtooClient> ws_protoo_client_;
    bool connected_ = false;
    int64_t last_connect_ms_ = -1;
    int64_t last_echo_ms_ = -1;
    uint64_t req_id_ = 0;

private:
    std::map<std::string, std::shared_ptr<Room>> rooms_;

private:
    std::mutex notification_mutex_; 
    std::queue<std::shared_ptr<RoomNotificationInfo>> room_notification_queue_;
};

}

#endif