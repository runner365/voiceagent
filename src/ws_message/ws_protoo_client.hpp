#ifndef WS_PROOTOO_CLIENT_HPP
#define WS_PROOTOO_CLIENT_HPP
#include "net/http/websocket/websocket_client.hpp"
#include "utils/logger.hpp"
#include <memory>
#include <string>

namespace cpp_streamer
{

class WsProtooClientCallbackI
{
public:
    virtual ~WsProtooClientCallbackI() = default;
public:
    virtual void OnConnected() = 0;
    virtual void OnResponse(const std::string& text) = 0;
    virtual void OnNotification(const std::string& text) = 0;
    virtual void OnClosed(int code, const std::string& reason) = 0;
};

class WsProtooClient : public WebSocketConnectionCallBackI
{
public:
    WsProtooClient(uv_loop_t* loop,
                   const std::string& hostname,
                   uint16_t port,
                   const std::string& subpath,
                   bool ssl_enable,
                   Logger* logger,
                   WsProtooClientCallbackI* cb = nullptr);

    // Initiate the websocket connection (separate from construction).
    // Caller must provide any necessary headers if required by protocol.
    virtual ~WsProtooClient();

public:
    void Reset();
    void AsyncConnect();
    // Send protoo request/notification; data_json should be a JSON fragment (object/value)
    void SendRequest(uint64_t id, const std::string& method, const std::string& data_json);
    void SendNotification(const std::string& method, const std::string& data_json);

protected: // WebSocketConnectionCallBackI
    virtual void OnConnection() override;
    virtual void OnReadData(int code, const uint8_t* data, size_t len) override;
    virtual void OnReadText(int code, const std::string& text) override;
    virtual void OnClose(int code, const std::string& desc) override;

private:
    std::unique_ptr<WebSocketClient> ws_client_ptr_;
    Logger* logger_ = nullptr;
    WsProtooClientCallbackI* cb_ = nullptr;
    bool connected_ = false;
};

}

#endif