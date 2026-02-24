#ifndef WS_PROTOO_INFO_HPP
#define WS_PROTOO_INFO_HPP
#include "utils/json.hpp"

#include <string>
#include <stdint.h>
#include <stddef.h>
#include <memory>
#include <iostream>

namespace cpp_streamer {

class ProtooResponseI;

class ProtooResponse
{
public:
    int id_;
    int code_;
    std::string error_msg_;

public:
    ProtooResponse(int id, int code, const std::string& err_msg, nlohmann::json& j)
        :id_(id), code_(code), error_msg_(err_msg), data_(j) {}
    ~ProtooResponse() {}

    nlohmann::json ToJson() {
        nlohmann::json j;
        j["id"] = id_;
        j["response"] = true;
        if (code_ != 0) {
            j["ok"] = false;
            j["errorCode"] = code_;
            j["errorReason"] = error_msg_;
            return j;
        }
        j["ok"] = true;
        j["data"] = data_;
        return j;
    }
private:
    nlohmann::json data_;
};

class ProtooCallBackI
{
public:
	virtual void OnProtooRequest(int id, const std::string& method, nlohmann::json& j, ProtooResponseI* resp_cb) = 0;
	virtual void OnProtooNotification(const std::string& method, nlohmann::json& j) = 0;
	virtual void OnProtooResponse(int id, int code, const std::string& err_msg, nlohmann::json& j) = 0;
	virtual void OnWsSessionClose(const std::string& room_id, const std::string& user_id) = 0;
};

class ProtooResponseI
{
public:
	virtual void OnProtooResponse(ProtooResponse& resp) = 0;
	virtual void Request(const std::string& method, nlohmann::json& j) = 0;
	virtual void Notification(const std::string& method, nlohmann::json& j) = 0;
	virtual void SetUserInfo(const std::string& room_id, const std::string& user_id) = 0;
};

typedef enum ProtooMessageType {
	PROTOO_MESSAGE_UNKNOWN,
	PROTOO_MESSAGE_REQUEST,
	PROTOO_MESSAGE_RESPONSE,
	PROTOO_MESSAGE_NOTIFICATION
} ProtooMessageType;

class ProtooRequestBase
{
public:
	bool request_;
	int id_;
	std::string method_;

public:
	static bool Parse(nlohmann::json& j, std::shared_ptr<ProtooRequestBase> obj) {
		try {
			auto reqIt = j.find("request");
			if (reqIt == j.end() || !reqIt->is_boolean() || !reqIt->get<bool>()) {
				return false;
			}
			obj->request_ = true;
            
			auto idIt = j.find("id");
			if (idIt == j.end() || !idIt->is_number_integer()) {
				return false;
			}
			obj->id_ = idIt->get<int>();

			auto methodIt = j.find("method");
			if (methodIt == j.end() || !methodIt->is_string()) {
				return false;
			}
			obj->method_ = methodIt->get<std::string>();
		}
		catch (std::exception& e) {
			std::cout << "ProtooRequestBase::Parse exception:" << e.what() << std::endl;
			return false;
		}
		return true;
	}
	static std::shared_ptr<ProtooRequestBase> FromJson(nlohmann::json& j) {
		std::shared_ptr<ProtooRequestBase> obj(new ProtooRequestBase());

		bool r = Parse(j, obj);
		if (!r) {
			return nullptr;
		}
		return obj;
	}
};

class JoinRequest : public ProtooRequestBase
{
public:
	std::string roomId_;
	std::string userId_;
	std::string userName_;

public:
	std::string Dump() {
		std::string info = "request_:" + std::to_string(request_) + ", ";
		info += "id_:" +  std::to_string(id_) + ", ";
		info += "method_:" + method_ + ", ";
		info += "roomId_:" + roomId_ + ", ";
		info += "userId_:" + userId_ + ", ";
		info += "userName_:" + userName_;
		return info;
	}
public:
	static std::shared_ptr<JoinRequest> FromJson(nlohmann::json& j) {
		std::shared_ptr<JoinRequest> req_ptr(new JoinRequest());

		try {
			bool r = Parse(j, req_ptr);
			if (!r) {
				return nullptr;
			}
			auto dataIt = j.find("data");
			if (dataIt == j.end() || !dataIt->is_object()) {
				return nullptr;
			}
            
			auto roomId = dataIt->find("roomId");
			if (roomId == dataIt->end() || !roomId->is_string()) {
				return nullptr;
			}
			req_ptr->roomId_ = roomId->get<std::string>();
            
			auto userId = dataIt->find("userId");
			if (userId == dataIt->end() || !userId->is_string()) {
				return nullptr;
			}
			req_ptr->userId_ = userId->get<std::string>();
            
			auto userName = dataIt->find("userName");
			if (userName == dataIt->end() || !userName->is_string()) {
				return nullptr;
			}
			req_ptr->userName_ = userName->get<std::string>();
		}
		catch (std::exception& e) {
			std::cout << "JoinRequest::FromJson parse exception:" << e.what() << std::endl;
			return nullptr;
		}

		return req_ptr;
	}
};

} // namespace cpp_streamer

#endif // WS_PROTOO_INFO_HPP