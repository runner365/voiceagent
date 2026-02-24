#include "ws_protoo_client.hpp"
#include "utils/logger.hpp"
#include "utils/json.hpp"

#include <map>

namespace cpp_streamer
{

using json = nlohmann::json;

WsProtooClient::WsProtooClient(uv_loop_t* loop,
                               const std::string& hostname,
                               uint16_t port,
                               const std::string& subpath,
                               bool ssl_enable,
                               Logger* logger,
                               WsProtooClientCallbackI* cb)
    : logger_(logger), cb_(cb)
{
    // Do not call AsyncConnect in constructor. Callers should invoke AsyncConnect()
    // when they are ready. Construct WebSocketClient instance now.
    ws_client_ptr_ = std::make_unique<WebSocketClient>(loop, hostname, port, subpath, ssl_enable, logger, this);
}

void WsProtooClient::AsyncConnect()
{
    if (!ws_client_ptr_) return;
    std::map<std::string, std::string> headers;
    // "Sec-WebSocket-Protocol", "protoo" for protoo support
    headers["Sec-WebSocket-Protocol"] = "protoo";
    ws_client_ptr_->AsyncConnect(headers);
}

void WsProtooClient::Reset()
{
    cb_ = nullptr;
}

WsProtooClient::~WsProtooClient()
{
    ws_client_ptr_.reset();
}

void WsProtooClient::SendRequest(uint64_t id, const std::string& method, const std::string& data_json)
{
    if (!ws_client_ptr_) return;
    try {
        json data = data_json.empty() ? json() : json::parse(data_json);
        json payload;
        payload["request"] = true;
        payload["id"] = id;
        payload["method"] = method;
        payload["data"] = data.is_null() ? json::object() : data;
        ws_client_ptr_->AsyncWriteText(payload.dump());
    } catch (const std::exception& e) {
        LogErrorf(logger_, "SendRequest JSON build error: %s", e.what());
    }
}

void WsProtooClient::SendNotification(const std::string& method, const std::string& data_json)
{
    if (!ws_client_ptr_) return;
    try {
        json data = data_json.empty() ? json() : json::parse(data_json);
        json payload;
        payload["notification"] = true;
        payload["method"] = method;
        payload["data"] = data.is_null() ? json::object() : data;
        ws_client_ptr_->AsyncWriteText(payload.dump());
    } catch (const std::exception& e) {
        LogErrorf(logger_, "SendNotification JSON build error: %s", e.what());
    }
}

void WsProtooClient::OnConnection()
{
    connected_ = true;
    LogInfof(logger_, "WsProtooClient connected");
    if (cb_) cb_->OnConnected();
}

void WsProtooClient::OnReadData(int code, const uint8_t* data, size_t len)
{
    // Not used; protoo uses text frames. Log unexpected binary.
    LogWarnf(logger_, "WsProtooClient received unexpected binary frame, len=%zu", len);
}

void WsProtooClient::OnReadText(int code, const std::string& text)
{
    // Parse JSON using utils/json.hpp (nlohmann::json)
    try {
        json j = json::parse(text);
        if (!j.is_object()) {
            LogWarnf(logger_, "Protoo text is not a JSON object: %s", text.c_str());
            return;
        }
        const bool is_response = j.value("response", false);
        const bool is_notification = j.value("notification", false);
        if (is_response) {
            LogDebugf(logger_, "Protoo response: %s", text.c_str());
            if (cb_) cb_->OnResponse(text);
            return;
        }
        if (is_notification) {
            LogDebugf(logger_, "Protoo notification: %s", text.c_str());
            if (cb_) cb_->OnNotification(text);
            return;
        }
        LogInfof(logger_, "Protoo text (unclassified): %s", text.c_str());
    } catch (const std::exception& e) {
        LogWarnf(logger_, "Failed to parse Protoo JSON: %s", e.what());
        LogInfof(logger_, "Raw text: %s", text.c_str());
    }
}

void WsProtooClient::OnClose(int code, const std::string& desc)
{
    connected_ = false;
    LogInfof(logger_, "WsProtooClient closed: code=%d, desc=%s", code, desc.c_str());
    if (cb_) cb_->OnClosed(code, desc);
}

}