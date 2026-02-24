#include "config.hpp"
#include <iostream>

namespace cpp_streamer
{
    // 静态实例指针
    Config* Config::config_instance = nullptr;

    // 析构函数
    Config::~Config()
    {
    }

    // 加载配置文件
    bool Config::Load(const std::string& config_file)
    {
        try {
            // 销毁旧实例
            if (config_instance) {
                delete config_instance;
                config_instance = nullptr;
            }

            // 创建新实例
            config_instance = new Config(config_file);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load config file: " << e.what() << std::endl;
            return false;
        }
    }

    // 获取单例实例
    Config& Config::Instance()
    {
        if (!config_instance) {
            throw std::runtime_error("Config not loaded");
        }
        return *config_instance;
    }

    // 私有构造函数，用于加载配置
    Config::Config(const std::string& config_file)
    {
        // 读取YAML配置文件
        YAML::Node config = YAML::LoadFile(config_file);

        // 加载日志配置
        if (config["log"]) {
            auto log = config["log"];
            log_config.log_level = log["level"].as<std::string>("INFO");
            log_config.log_file = log["file"].as<std::string>("voiceagent.log");
        }

        // 加载WebSocket服务器配置
        if (config["ws_server"]) {
            auto ws_config = config["ws_server"];
            ws_server_config.host = ws_config["host"].as<std::string>("0.0.0.0");
            ws_server_config.port = ws_config["port"].as<uint16_t>(8080);
            ws_server_config.enable_ssl = ws_config["enable_ssl"].as<bool>(false);
            ws_server_config.subpath = ws_config["subpath"].as<std::string>("/ws");
        }

        // 加载TTS配置
        if (config["tts_config"]) {
            auto tts_config_yaml = config["tts_config"];
            tts_config.tts_enable = tts_config_yaml["tts_enable"].as<bool>(false);
            tts_config.acoustic_model = tts_config_yaml["acoustic_model"].as<std::string>("");
            tts_config.vocoder = tts_config_yaml["vocoder"].as<std::string>("");
            tts_config.lexicon = tts_config_yaml["lexicon"].as<std::string>("");
            tts_config.tokens = tts_config_yaml["tokens"].as<std::string>("");
            tts_config.dict_dir = tts_config_yaml["dict_dir"].as<std::string>("");
            tts_config.num_threads = tts_config_yaml["num_threads"].as<int32_t>(1);
        }
    }

    std::string Config::Dump() const
    {
        std::stringstream ss;

        // 日志配置
        ss << "LogConfig:\n";
        ss << "  level: " << log_config.log_level << "\n";
        ss << "  file: " << log_config.log_file << "\n";
        
        // WebSocket服务器配置
        ss << "WsServerConfig:\n";
        ss << "  host: " << ws_server_config.host << "\n";
        ss << "  port: " << ws_server_config.port << "\n";
        ss << "  enable_ssl: " << ws_server_config.enable_ssl << "\n";
        ss << "  subpath: " << ws_server_config.subpath << "\n";

        // TTS配置
        ss << "TtsConfig:\n";
        ss << "  tts_enable: " << tts_config.tts_enable << "\n";
        ss << "  acoustic_model: " << tts_config.acoustic_model << "\n";
        ss << "  vocoder: " << tts_config.vocoder << "\n";
        ss << "  lexicon: " << tts_config.lexicon << "\n";
        ss << "  tokens: " << tts_config.tokens << "\n";
        ss << "  dict_dir: " << tts_config.dict_dir << "\n";
        ss << "  num_threads: " << tts_config.num_threads << "\n";

        return ss.str();
    }
}