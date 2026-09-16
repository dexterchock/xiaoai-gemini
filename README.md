# xiaoai-gemini

在 Xiaomi 智能音箱 Pro（OH2P）上运行基于 **Google Gemini Live API** 的本地实时语音助手。

本项目基于 [kslr/xiaoai-plus](https://github.com/kslr/xiaoai-plus) 进行二次开发，保留原项目的音频采集、AEC、KWS、应用与部署架构，并将原有的 Doubao 实时 AI 后端替换为 Gemini Live API。

## 支持设备

| 型号 | 设备代号 |
| --- | --- |
| Xiaomi 智能音箱 Pro | OH2P |

> ⚠️ 当前项目以 **OH2P** 为目标设备。其他小爱音箱型号没有经过本项目验证，请不要直接套用。

## 特性

- **Gemini Live 实时语音**：通过 WebSocket 直接连接 Google Gemini Live API，实现双向实时语音对话。
- **端侧运行**：核心程序直接运行在音箱本机，不需要额外搭建中转服务器。
- **实时打断**：支持在 Gemini 回复过程中再次说话并打断当前语音输出。
- **连续对话**：保持 Gemini Live 会话，实现连续的语音交互。
- **自定义系统提示词**：可以在 `config.ini` 中配置助手角色与回复风格。
- **自定义声音**：通过配置选择 Gemini 的预置语音。
- **自定义唤醒词**：继续使用项目现有的 sherpa-onnx KWS，可根据需要替换关键词。
- **远场音频处理**：继续使用原项目的 AEC（回声消除）、NS（降噪）和 AGC（自动增益）。
- **保留小爱同学**：本项目只负责新增 Gemini 助手，不修改音箱原有的小爱能力；具体共存方式取决于你的补丁与唤醒词配置。

## Gemini 模型

本项目默认使用：

```ini
model = gemini-3.8-live
```

`gemini-3.8-live` 是 Google 当前用于低延迟实时语音交互的稳定 Live 模型。Gemini Live API 使用原生音频输出；当前文档示例的输出采样率为 **24 kHz**，输入语音使用 **16 kHz PCM**。  
参考：[Gemini 3.8 Live](https://ai.google.dev/gemini-api/docs/models/gemini-3.8-live) · [Live API](https://ai.google.dev/gemini-api/docs/live-api/get-started-websocket)

## 前置准备

在开始之前，你需要：

1. 一台已经可以 SSH 登录的 Xiaomi 智能音箱 Pro（OH2P）。
2. 已刷入支持本项目运行方式的音箱补丁固件。
3. 一个可用的 Google Gemini API Key。
4. 当前网络环境能够访问 Gemini Live API。

### 刷机与 SSH

如果你还没有完成音箱补丁和 SSH 配置，请先参考：

[open-xiaoai 刷机教程](https://github.com/idootop/open-xiaoai/blob/main/docs/flash.md)

请注意：刷机、补丁与 SSH 环境属于音箱基础设施，不是本项目本身负责的部分。

## 获取项目

你可以直接克隆本项目：

```sh
git clone https://github.com/dexterchock/xiaoai-gemini.git
cd xiaoai-gemini
```

如果你已经把项目下载为 ZIP，也可以直接进入解压后的项目目录。

## 配置 Gemini API Key

前往 Google AI Studio 创建 API Key：

https://aistudio.google.com/apikey

然后编辑项目中的 `config.ini`。

最基本的配置示例：

```ini
[realtime]
api_key = your_google_api_key
model = gemini-3.8-live
bot_name = Gemini
system_role = 你是 Gemini 同学，是运行在小爱音箱上的实时语音助手。
speaking_style = 语气自然、友好、简洁。
voice = Kore

[wakeup]
say_hello = 你好
```

### 配置说明

| 配置项 | 说明 |
| --- | --- |
| `api_key` | Google Gemini API Key |
| `model` | Gemini Live 模型，推荐 `gemini-3.8-live` |
| `bot_name` | 助手名称 |
| `system_role` | 系统提示词 / 助手角色 |
| `speaking_style` | 回复风格 |
| `voice` | Gemini 预置语音名称 |
| `say_hello` | 唤醒后的问候语 |

请不要把包含真实 API Key 的 `config.ini` 提交到公开 GitHub 仓库。

## 在电脑上本地测试

本项目是 C++ 项目。建议先在电脑上完成编译和 Gemini API 连接测试，再部署到音箱。

项目使用 CMake 构建，并依赖项目现有的第三方组件，包括：

- nlohmann/json
- IXWebSocket
- spdlog
- OpenSSL
- zlib
- WebRTC Audio Processing
- sherpa-onnx

Windows 本地测试时，可以使用 Visual Studio 的开发者命令行或 Visual Studio Code 配合 CMake 工具进行构建。

基本流程：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

具体构建方式可能取决于你当前使用的编译器、依赖环境以及项目的交叉编译配置。

> 注意：电脑上的本地测试主要用于验证 Gemini Live 连接、协议、配置和代码逻辑。OH2P 的实际麦克风、扬声器、AEC、KWS 和设备相关行为仍需要在真实音箱上测试。

## 部署到 OH2P

### 方式 A：使用项目发布版本

如果本项目提供 GitHub Releases，请下载与你的 OH2P 架构对应的最新构建产物，然后上传到音箱。

例如：

```sh
scp xiaoai_plus_speaker root@<SPEAKER_IP>:/data/xiaoai-gemini/
```

然后 SSH 登录音箱：

```sh
ssh root@<SPEAKER_IP>
```

赋予执行权限：

```sh
chmod +x /data/xiaoai-gemini/xiaoai_plus_speaker
```

### 方式 B：从源码交叉编译

如果你需要自己构建 OH2P 版本，请使用项目提供的交叉编译工具链和 CMake 配置。

构建完成后，将生成的 `xiaoai_plus_speaker`、`config.ini` 以及项目需要的资源文件复制到音箱，例如：

```text
/data/xiaoai-gemini/
├── xiaoai_plus_speaker
├── config.ini
└── assets/
```

实际文件布局请以你当前构建生成的产物为准。

## 在音箱上运行

SSH 登录 OH2P 后：

```sh
cd /data/xiaoai-gemini
./xiaoai_plus_speaker -c ./config.ini
```

如果程序启动成功，你应该能看到 WebSocket / Gemini 会话建立以及音频处理相关日志。

### 调试建议

第一次部署时，建议直接在 SSH 终端前台运行：

```sh
./xiaoai_plus_speaker -c ./config.ini
```

这样可以直接观察：

- 配置加载是否正常
- Gemini WebSocket 是否连接成功
- Gemini `setupComplete` 是否收到
- 音频是否正常发送
- Gemini 是否返回音频
- 打断和重新对话是否正常

确认运行稳定后，再设置开机自启动。

## 开机自启动

项目包含 `boot.sh`。如果你需要让程序随音箱启动，请根据你当前 OH2P 补丁环境，将 `boot.sh` 配置到系统的启动流程中。

示例：

```sh
cp boot.sh /data/init.sh
chmod +x /data/init.sh
```

然后重启：

```sh
reboot
```

> `boot.sh` 的具体启动方式依赖你的 OH2P 补丁环境。建议先确认手动启动完全正常，再启用开机自启动。

## 自定义唤醒词

本项目继续使用 sherpa-onnx KWS。

如果需要修改唤醒词，可以在开发机上重新生成 `assets/keywords.txt`。

参考官方 sherpa-onnx KWS 文档：

https://k2-fsa.github.io/sherpa/onnx/kws/pretrained_models/index.html

例如：

```sh
python3 -m pip install -U sherpa-onnx click sentencepiece pypinyin
```

编辑关键词：

```text
Gemini 同学 @Gemini 同学
小爱同学 @小爱同学
LIGHT UP @LIGHT_UP
```

然后重新生成 token：

```sh
sherpa-onnx-cli text2token \
  --tokens assets/tokens.txt \
  --tokens-type phone+ppinyin \
  --lexicon assets/en.phone \
  assets/keywords_raw.txt assets/keywords.txt
```

重新部署生成后的资源文件即可。

## 音频架构

本项目保留了原 `xiaoai-plus` 的主要本地音频处理路径：

```text
OH2P 麦克风
    ↓
16 kHz 多通道采集
    ↓
AEC / NS / AGC
    ↓
唤醒词检测
    ↓
Gemini Live WebSocket
    ↓
Gemini 原生音频输出
    ↓
OH2P 播放
```

Gemini Live API：

- 输入：16-bit PCM，16 kHz
- 输出：原生音频，当前模型文档示例为 24 kHz

因此，Gemini 网络协议层与 OH2P 本地音频处理层是分开的；不要为了更换 AI 后端而随意修改 AEC、KWS 或麦克风采集参数。

## 与原项目的关系

本项目基于：

[kslr/xiaoai-plus](https://github.com/kslr/xiaoai-plus)

原项目使用 Doubao Realtime API。

本项目的主要变化是：

- 移除 Doubao 实时协议及相关帧编解码逻辑
- 使用 Gemini Live WebSocket API
- 使用 Google Gemini API Key 进行鉴权
- 使用 Gemini Live 的 JSON 消息格式
- 接收 Gemini 原生实时音频
- 保留原项目的音频、AEC、KWS、应用和构建架构

因此，本项目不是从零开始重写的小爱音箱系统，而是基于 `xiaoai-plus` 的 Gemini Live 后端移植。

## 常见问题

### Gemini 连接失败

首先确认：

1. API Key 正确。
2. Google AI Studio 中的 Key 可以正常调用 Gemini API。
3. 当前网络环境可以访问：
   `generativelanguage.googleapis.com`
4. `model` 配置正确：

```ini
model = gemini-3.8-live
```

### 程序可以启动，但没有声音

检查：

- OH2P 的本地音频设备是否正常。
- AEC / KWS 是否正常启动。
- Gemini WebSocket 是否成功建立。
- 是否收到 `setupComplete`。
- Gemini 是否返回音频数据。
- 播放设备和采样率配置是否正确。

### 如何确认 Gemini 已经连接

运行程序时观察日志。正常情况下，应先完成 WebSocket 连接，然后发送 Gemini `setup`，收到 `setupComplete` 后才开始发送后续实时输入。

### 我可以换其他 Gemini Live 模型吗？

可以，但请确认该模型支持 Gemini Live API 及你当前使用的配置。

本项目当前默认针对：

```text
gemini-3.8-live
```

进行配置。

## 安全提示

不要公开以下信息：

- Gemini API Key
- 包含真实 API Key 的 `config.ini`
- 音箱 SSH 密码或私钥
- 其他敏感凭据

推荐将本地配置保留在自己的设备上，不要提交到公开仓库。

## 免责声明

本项目仅供学习、研究和个人实验使用。

- 本项目与 Xiaomi / 小爱音箱官方无从属关系。
- 本项目与 Google / Gemini 官方无从属关系。
- Xiaomi、Google、Gemini、小爱等名称及商标归各自权利人所有。
- 使用第三方 API 时，请遵守当地法律法规以及相关服务条款。
- 刷机、修改音箱系统和使用第三方软件可能存在风险，请自行承担相关风险。

## License

MIT

## 致谢

感谢：

- [kslr/xiaoai-plus](https://github.com/kslr/xiaoai-plus)
- [Open-XiaoAI](https://github.com/idootop/open-xiaoai)
- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)
- [Google Gemini API](https://ai.google.dev/gemini-api/docs/live-api)

