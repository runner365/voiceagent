# Voice Agent - 实时语音对话 AI 智能体

![Voice Agent](https://img.shields.io/badge/Voice-Agent-blue.svg)
![Python](https://img.shields.io/badge/Python-3.8+-green.svg)
![AI](https://img.shields.io/badge/AI-Real--time-orange.svg)

## 项目简介

**Voice Agent** 是一个先进的「实时语音对话 AI」智能体，具备以下核心能力：

- 🎤 **会说话** - 支持自然流畅的语音合成
- 👂 **能听懂** - 高精度的语音识别能力
- 💬 **能主动交互** - 智能对话管理和上下文理解
- ⚡ **实时响应** - 低延迟的语音处理和反馈
- 🔧 **高度可定制** - 支持多种 LLM 模型和配置选项

## 技术架构

Voice Agent 采用模块化设计，主要组件包括：

- **核心服务** (`voice_agent.py`) - 主服务器入口，负责协调各个模块
- **会话管理** - 处理用户会话和对话状态
- **模型管理** - 管理 ASR (自动语音识别) 模型
- **WebSocket 服务** - 通过接入RTPPilot(WebRTC SFU)，实现实时语音传输和处理, RTPPilot开源地址: [https://github.com/runner365/RTCPilot](https://github.com/runner365/RTCPilot)
- **Worker 管理** - 处理语音处理等计算密集型任务
- **配置系统** - 灵活的 YAML 配置管理

## 支持的 LLM 模型

- **Qwen** (通义千问)
- **Yuanbao** (元宝)
- **OpenAI** (GPT 系列)
- **DeepSeek** (深度求索)

```
{
    "qwen": {
        "llm_type": "qwen",
        "url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "model": "qwen-plus",
    },
    "yuanbao": {
        "llm_type": "yuanbao",
        "url": "https://api.hunyuan.cloud.tencent.com/v1",
        "model": "hunyuan-turbo",
    },
    "openai": {
        "llm_type": "openai",
        "url": "https://api.openai.com/v1",
        "model": "gpt-3.5-turbo",
    },
    "deepseek": {
        "llm_type": "deepseek",
        "url": "https://api.deepseek.cn/v1",
        "model": "deepseek-turbo",
    },
}
```

export LLM_API_KEY=your_api_key

在系统环境变量中添加 LLM_API_KEY 变量，值为你的 API 密钥。

## 快速开始

### 环境要求

- Python 3.8+
- 操作系统：Linux / macOS / Windows
- 依赖库：见 `requirements.txt`

### 安装步骤

1. **克隆项目**

```bash
git clone https://github.com/runner365/voiceagent
cd voiceagent
```

2. **安装依赖**

```bash
pip install -r requirements.txt
```

3. **配置模型**

项目使用 FunASR 模型进行语音识别，首次运行时会自动下载所需模型。

运行：

```bash
python funasr_download.py
```

### 运行服务

1. **修改配置文件**

根据需要修改 `config.yaml` 文件：

```yaml
# 日志配置
log:
  log_level: "info"  # debug, info, warn, error
  log_path: "server.log"

# WebSocket 服务器配置
protoo_server:
  port: 5555
  ssl_enable: false
  cert_path: "certificate.crt"
  key_path: "private.key"
  subpath: "voiceagent"

# LLM 配置
llm_config:
  llm_type: "qwen"  # qwen, yuanbao, openai, deepseek
  model_name: ""  # 使用默认模型，或指定具体模型

# Worker 配置
worker_config:
  worker_bin: "./objs/voiceagent"
  config_path: "./src/transcode.yaml"
```

2. **启动服务**

```bash
python voice_agent.py config.yaml
```

服务启动后，会在指定端口（默认 5555）上运行 WebSocket 服务器，等待客户端连接。

## API 接口

Voice Agent 通过 WebSocket 提供实时通信接口，支持以下功能：

- **语音流传输** - 实时传输音频数据
- **文本消息** - 发送和接收文本消息
- **会话管理** - 创建、维护和结束会话
- **状态查询** - 获取系统和会话状态

## 项目结构

```
voiceagent/
├── voice_agent.py        # 主入口文件
├── config/               # 配置相关
│   └── config.py         # 配置管理类
├── config.yaml           # 默认配置文件
├── logger/               # 日志系统
├── agent_session/        # 会话管理
├── websocket_protoo/     # WebSocket 服务
├── utils/                # 工具类
│   └── model_manager.py  # 模型管理
├── worker_mgr/           # Worker 管理
├── 3rdparty/             # 第三方依赖
└── README.md             # 项目说明
```

## 核心功能

### 1. 实时语音识别

- 支持流式语音输入
- 低延迟识别结果
- 多语言支持

### 2. 智能对话管理

- 上下文理解和记忆
- 自然语言处理
- 多轮对话支持

### 3. 语音合成

- 自然流畅的语音输出
- 可调整的语音参数
- 多种语音风格

### 4. 模型集成

- 支持多种 LLM 后端
- 可插拔的模型架构
- 模型自动管理

## 配置说明

### 环境变量

无特殊环境变量要求，所有配置通过 `config.yaml` 文件管理。

### 主要配置项

| 配置项 | 说明 | 默认值 |
|-------|------|-------|
| log.log_level | 日志级别 | "info" |
| log.log_path | 日志文件路径 | "server.log" |
| protoo_server.port | WebSocket 服务端口 | 5555 |
| protoo_server.ssl_enable | 是否启用 SSL | false |
| llm_config.llm_type | LLM 类型 | "qwen" |
| llm_config.model_name | LLM 模型名称 | "" (使用默认) |
| worker_config.worker_bin | Worker 可执行文件路径 | "./objs/voiceagent" |

## 开发指南

### 扩展 LLM 支持

要添加新的 LLM 支持，需要：

1. 在 `llm_config.llm_type` 中添加新类型
2. 实现对应的模型接口
3. 更新配置管理类

### 自定义语音处理

可以通过修改 Worker 配置和实现来定制语音处理逻辑：

1. 编辑 `worker_config` 部分
2. 修改或替换 Worker 实现

## 部署建议

### 生产环境

- 使用 SSL 加密保护通信
- 配置适当的日志级别和存储
- 监控系统资源使用情况
- 考虑使用负载均衡处理高并发

### 性能优化

- 使用硬件加速（如 GPU）处理语音和 LLM 任务
- 合理配置 Worker 数量
- 优化网络带宽和延迟

## 故障排查

### 常见问题

1. **服务启动失败**
   - 检查端口是否被占用
   - 验证配置文件格式是否正确
   - 查看日志文件了解详细错误

2. **语音识别不准确**
   - 确保音频质量良好
   - 检查模型是否正确加载
   - 调整音频输入参数

3. **响应延迟高**
   - 检查网络连接
   - 验证系统资源使用情况
   - 考虑使用更强大的硬件

### 日志系统

日志文件默认存储在 `server.log`，包含详细的系统运行信息和错误记录。

## 许可证

[MIT License](LICENSE)

## 贡献

欢迎提交 Issue 和 Pull Request 来改进这个项目！

## 联系方式

- 项目维护者：[Your Name]
- 邮箱：[your.email@example.com]
- GitHub：[repository-url]

---

**Voice Agent** - 让 AI 对话更自然、更智能、更实时！ 🚀