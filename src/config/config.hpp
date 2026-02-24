#ifndef CONFIG_HPP
#define CONFIG_HPP
#include <string>
#include <yaml-cpp/yaml.h>
#include <stdint.h>
#include <stddef.h>


namespace cpp_streamer
{

/*
tts_config:
  tts_enable: true
  acoustic_model: "./matcha-icefall-zh-baker/model-steps-3.onnx"
  vocoder: "./vocos-22khz-univ.onnx"
  lexicon: "./matcha-icefall-zh-baker/lexicon.txt"
  tokens: "./matcha-icefall-zh-baker/tokens.txt"
  dict_dir: "./matcha-icefall-zh-baker/dict"
  num_threads: 1
*/

class TtsConfig
{
public:
    TtsConfig() = default;
    ~TtsConfig() = default;

public:
    bool tts_enable;
    std::string acoustic_model;
    std::string vocoder;
    std::string lexicon;
    std::string tokens;
    std::string dict_dir;
    int32_t num_threads;
};

class LogConfig
{
public:
    LogConfig() = default;
    ~LogConfig() = default;

public:
    std::string log_level;
    std::string log_file;
};

class WsServerConfig
{
public:
    WsServerConfig() = default;
    ~WsServerConfig() = default;

public:
    std::string host;
    uint16_t port;
    bool enable_ssl;
    std::string subpath;
};

class Config
{
public:
    ~Config();

public:
    static bool Load(const std::string& config_file);
    static Config& Instance();

public:
    std::string Dump() const;

private:
    Config(const std::string& config_file);

private:
    static Config* config_instance;
public:
    LogConfig log_config;
public:
    WsServerConfig ws_server_config;
public:
    TtsConfig tts_config;
};

}

#endif