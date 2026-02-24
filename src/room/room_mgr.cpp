#include "room_mgr.hpp"
#include "room.hpp"
#include "config/config.hpp"
#include "ws_message/ws_protoo_client.hpp"
#include "utils/timeex.hpp"
#include "utils/json.hpp"
#include "utils/base64.hpp"
#include "utils/data_buffer.hpp"

using nlohmann::json;

namespace cpp_streamer {

RoomMgr* RoomMgr::instance_ = nullptr;

RoomMgr::RoomMgr(uv_loop_t* loop, Logger* logger) : TimerInterface(10),
    loop_(loop), logger_(logger) {
    LogInfof(logger_, "RoomMgr constructor");
    StartTimer();
}

RoomMgr::~RoomMgr() {
    LogInfof(logger_, "RoomMgr destructor");
    StopTimer();
}

RoomMgr* RoomMgr::Instance() {
    return instance_;
}

bool RoomMgr::OnTimer() {
    try {
        Connect();
        EchoRequest();
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr OnTimer failed, ret: %s", e.what());
    }

    try {
        OnSendPcmData2VoiceAgent();
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr OnTimer failed, ret: %s", e.what());
    }

    try {
        OnCheckRoomAlive();
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr OnTimer failed, ret: %s", e.what());
    }
    return true;
}

// implement WsProtooClientCallbackI
void RoomMgr::OnConnected() {
    connected_ = true;
    LogInfof(logger_, "RoomMgr OnConnected");
}

void RoomMgr::OnResponse(const std::string& text) {
    LogDebugf(logger_, "RoomMgr OnResponse text: %s", text.c_str());
}

void RoomMgr::OnNotification(const std::string& text) {
    LogDebugf(logger_, "RoomMgr OnNotification text: %s", text.c_str());

    try {
        json j = json::parse(text);
        if (!j.contains("notification") || !j["notification"].is_boolean() || !j["notification"]) {
            LogErrorf(logger_, "RoomMgr OnNotification invalid notification: %s", text.c_str());
            return;
        }
        if (!j.contains("method") || !j["method"].is_string()) {
            LogErrorf(logger_, "RoomMgr OnNotification invalid method: %s", text.c_str());
            return;
        }
        std::string method = j["method"];
        if (!j.contains("data") || !j["data"].is_object()) {
            LogErrorf(logger_, "RoomMgr OnNotification invalid data: %s", text.c_str());
            return;
        }
        if (method == "opus_data") {
            //opus data from sfu
            OnHandleOpusData(j["data"]);
        } else if (method == "response.text") {
            OnHandleResponseText(j["data"]);
        } else {
            LogErrorf(logger_, "RoomMgr OnNotification unhandled method: %s", method.c_str());
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr OnNotification failed, ret: %s", e.what());
    }
}

void RoomMgr::OnHandleResponseText(const json& j) {
    try {
        std::string room_id = j["roomId"];
        std::string user_id = j["userId"];
        std::string text = j["text"];

        if (room_id.empty()) {
            LogErrorf(logger_, "RoomMgr Handle Response Text invalid room_id: %s", room_id.c_str());
            return;
        }
        if (user_id.empty()) {
            LogErrorf(logger_, "RoomMgr Handle Response Text invalid user_id: %s", user_id.c_str());
            return;
        }
        if (text.empty()) {
            LogErrorf(logger_, "RoomMgr Handle Response Text invalid text: %s", text.c_str());
            return;
        }
        std::shared_ptr<Room> room = GetorCreateRoom(room_id);
        room->OnHandleResponseText(user_id, text);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr OnHandleResponseText failed, ret: %s", e.what());
    }
}

void RoomMgr::OnHandleOpusData(const json& j) {
    try {
        std::string type_str = j["type"];
        if (type_str != "opus_data") {
            LogErrorf(logger_, "RoomMgr Handle Opus Data invalid type: %s", type_str.c_str());
            return;
        }
        std::string room_id = j["roomId"];
        std::string user_id = j["userId"];

        if (room_id.empty()) {
            LogErrorf(logger_, "RoomMgr Handle Opus Data invalid room_id: %s", room_id.c_str());
            return;
        }
        if (user_id.empty()) {
            LogErrorf(logger_, "RoomMgr HandleOpusData invalid user_id: %s", user_id.c_str());
            return;
        }
        std::string opus_base64 = j["opus_base64"];

        std::string opus_data = Base64Decode(opus_base64);

        if (opus_data.empty()) {
            LogErrorf(logger_, "RoomMgr Handle Opus Data invalid opus_data: %s", opus_base64.c_str());
            return;
        }
        DATA_BUFFER_PTR opus_buffer = std::make_shared<DataBuffer>();
        opus_buffer->AppendData(opus_data.data(), opus_data.size());

        LogDebugf(logger_, "RoomMgr Handle Opus Data room_id: %s, user_id: %s, opus_data len:%zu", 
            room_id.c_str(), user_id.c_str(), opus_data.size());
        std::shared_ptr<Room> room = GetorCreateRoom(room_id);
        room->OnHanldeOpusData(user_id, opus_buffer);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr OnHandleOpusData failed, ret: %s", e.what());
    }
}

void RoomMgr::OnClosed(int code, const std::string& reason) {
    LogInfof(logger_, "RoomMgr OnClosed code: %d, reason: %s", code, reason.c_str());
}

int RoomMgr::Initialize(uv_loop_t* loop, Logger* logger) {
    if (instance_) {
        return -1;
    }
    instance_ = new RoomMgr(loop, logger);

    instance_->ws_protoo_client_.reset(new WsProtooClient(instance_->loop_, 
        Config::Instance().ws_server_config.host,
        Config::Instance().ws_server_config.port,
        Config::Instance().ws_server_config.subpath,
        Config::Instance().ws_server_config.enable_ssl,
        instance_->logger_, instance_));
    return 0;
}

int RoomMgr::Connect() {
    if (connected_) {
        return 0;
    }
    int64_t now_ms = now_millisec();
    if (now_ms - last_connect_ms_ < 5000) {
        return 0;
    }
    try {
        ws_protoo_client_->AsyncConnect();
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr Connect failed, ret: %s", e.what());
        return -1;
    }
    last_connect_ms_ = now_ms;
    return 0;
}

void RoomMgr::EchoRequest() {
    if (!connected_) {
        Connect();
        return;
    }
    int64_t now_ms = now_millisec();
    if (now_ms - last_echo_ms_ < 15*1000) {
        return;
    }
    last_echo_ms_ = now_ms;

    try {
        json j = json::object();
        j["method"] = "echo";
        j["ts"] = now_ms;
        j["type"] = "voiceagent_worker";
        ws_protoo_client_->SendRequest(req_id_++, "echo", j.dump());
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RoomMgr EchoRequest failed, ret: %s", e.what());
    }
}

void RoomMgr::OnCheckRoomAlive() {
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        std::shared_ptr<Room> room = it->second;
        if (!room->IsAlive()) {
            LogInfof(logger_, "Room %s is not alive, remove it", room->GetRoomId().c_str());
            it->second->Close();
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
}
std::shared_ptr<Room> RoomMgr::GetorCreateRoom(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second;
    }
    std::shared_ptr<Room> room = std::make_shared<Room>(room_id, this, logger_);
    rooms_[room_id] = room;
    return room;
}

void RoomMgr::EraseRoom(const std::string& room_id) {
    rooms_.erase(room_id);
}

void RoomMgr::Notification2VoiceAgent(std::shared_ptr<RoomNotificationInfo> info_ptr) {
    LogDebugf(logger_, "RoomMgr OnNotification room_id: %s, user_id: %s, msg: %s", 
        info_ptr->room_id.c_str(), info_ptr->user_id.c_str(), info_ptr->msg.c_str());

    InsertRoomNotification(info_ptr);
}

void RoomMgr::InsertRoomNotification(std::shared_ptr<RoomNotificationInfo> info_ptr) {
    std::lock_guard<std::mutex> lock(notification_mutex_);
    room_notification_queue_.push(info_ptr);
}

bool RoomMgr::PopRoomNotification(std::vector<std::shared_ptr<RoomNotificationInfo>>& info_vec) {
    std::lock_guard<std::mutex> lock(notification_mutex_);
    if (room_notification_queue_.empty()) {
        return false;
    }
    while (!room_notification_queue_.empty()) {
        info_vec.push_back(room_notification_queue_.front());
        room_notification_queue_.pop();
    }
    return true;
}

size_t RoomMgr::GetRoomNotificationSize() {
    std::lock_guard<std::mutex> lock(notification_mutex_);
    return room_notification_queue_.size();
}

void RoomMgr::OnSendPcmData2VoiceAgent() {
    std::vector<std::shared_ptr<RoomNotificationInfo>> info_vec;
    if (!PopRoomNotification(info_vec)) {
        return;
    }
    for (auto& info_ptr : info_vec) {
        json j = json::object();
        j["method"] = info_ptr->method;
        j["ts"] = now_millisec();
        j["roomId"] = info_ptr->room_id;
        j["userId"] = info_ptr->user_id;
        j["msg"] = info_ptr->msg;
        if (info_ptr->task_index > 0) {
            j["taskIndex"] = info_ptr->task_index;
        }

        LogDebugf(logger_, "RoomMgr OnSendPcmData2VoiceAgent msg: %s", j.dump().c_str());
        ws_protoo_client_->SendNotification(info_ptr->method, j.dump());
    }
}

}