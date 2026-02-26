#include "yaml-cpp/yaml.h"
#include "config/config.hpp"
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include "net/http/http_server.hpp"
#include "room/room_mgr.hpp"
#include <iostream>
#include <uv.h>

using namespace cpp_streamer;

enum LOGGER_LEVEL GetLogLevelFromString(const std::string& level_str) {
    if (level_str == "debug") {
        return LOGGER_DEBUG_LEVEL;
    } else if (level_str == "info") {
        return LOGGER_INFO_LEVEL;
    } else if (level_str == "warn") {
        return LOGGER_WARN_LEVEL;
    } else if (level_str == "error") {
        return LOGGER_ERROR_LEVEL;
    }
    return LOGGER_INFO_LEVEL;
}

static void EchoMessageHandle(const HttpRequest* request, std::shared_ptr<HttpResponse> response_ptr) {
    std::string data = std::string(request->content_body_, request->content_length_);

    LogInfof(request->GetLogger(), "echo message: %s", data.c_str());

    response_ptr->Write((char*)data.c_str(), data.length());
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    // 加载配置文件
    if (!Config::Load(argv[1])) {
        return 1;
    }
    // 打印配置
    Config& config = Config::Instance();
    std::cout << "Config loaded successfully: " << config.Dump() << std::endl;
    std::unique_ptr<Logger> logger = std::make_unique<Logger>(
        config.log_config.log_file, GetLogLevelFromString(config.log_config.log_level), true);

    LogInfof(logger.get(), config.Dump().c_str());
    LogInfof(logger.get(), "logger level: %s, log file: %s", config.log_config.log_level.c_str(), config.log_config.log_file.c_str());
    LogInfof(logger.get(), "uv_run start");
    std::this_thread::sleep_for(std::chrono::seconds(5));

	uv_loop_t* loop = uv_default_loop();
    TimerInner::GetInstance()->Initialize(loop, 5);

    std::unique_ptr<HttpServer> http_server = std::make_unique<HttpServer>(loop, 
        "0.0.0.0", 9931, logger.get());
    http_server->AddPostHandle("/echo", EchoMessageHandle);


    int r = RoomMgr::Initialize(loop, logger.get());
    if (r != 0) {
        LogErrorf(logger.get(), "RoomMgr Initialize failed, ret: %d", r);
        return 1;
    }
    
    uv_run(loop, UV_RUN_DEFAULT);
    std::cout << "uv_run exit" << std::endl;
    return 0;
}