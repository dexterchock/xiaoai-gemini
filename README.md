# xiaoai-gemini

在小爱音箱（OH2P）上获得基于 **Google Gemini Live API** 的端侧实时语音对话体验。

本项目基于 [kslr/xiaoai-plus](https://github.com/kslr/xiaoai-plus) 进行二次开发，保留原项目的音频采集、AEC、KWS、应用与部署架构，仅将原有的 Doubao 实时 AI 后端替换为 Gemini Live API。

## 支持设备

| 型号 | 设备代号 |
| --- | --- |
| Xiaomi 智能音箱 Pro | OH2P |

> ⚠️ 其他小爱音箱型号未经本项目验证，请不要直接套用。

## 特性

- **Gemini Live 实时对话**：通过 WebSocket 直连 Gemini Live API，支持实时互动、连续对话与随时打断。
- **双助手共存**：小爱同学与 Gemini 同学可同时运行，拥有独立的唤醒词。
- **纯本地运行**：程序完全在音箱本机执行，无需搭建外部中转服务器。
- **自定义唤醒**：支持根据需求自定义关键词进行语音唤醒。
- **远场优化**：集成 AEC（回声消除）、NS（降噪）、AGC（增益），提升远场唤醒与对话的准确率。

## 快速开始

1. 刷机更新小爱音箱补丁固件，开启并 SSH 连接到小爱音箱 👉 [教程](https://github.com/idootop/open-xiaoai/blob/main/docs/flash.md)
2. 前往 [Google AI Studio](https://aistudio.google.com/apikey) 创建一个 Gemini API Key

   目前 Gemini Live API 提供免费额度，登录后即可创建 Key
3. 执行安装脚本（会自动下载并安装最新 release 到 `/data/xiaoai-plus`）
   ```sh
   curl -sSfL https://fastly.jsdelivr.net/gh/dexterchock/xiaoai-gemini@main/install.sh | sh
   ```
4. 更新 `config.ini` 里的配置
   ```ini
   [realtime]
   api_key = your_google_api_key
   model = gemini-3.8-live
   bot_name = Gemini
   system_role = 你是 Gemini 同学，是小爱音箱上的语音助手。
   speaking_style = 语气自然、友好、简洁。
   voice = Kore

   [wakeup]
   say_hello = 在

   [audio]
   playback_gain = 1.0
   ```
5. **(可选)** 设置自定义关键词（在项目根目录执行，参考 sherpa 文档：https://k2-fsa.github.io/sherpa/onnx/kws/pretrained_models/index.html#）
   ```shell
   # 安装 sherpa-onnx-cli（在开发机执行，不在小爱音箱上执行）
   python3 -m pip install -U sherpa-onnx click sentencepiece pypinyin

   cat <<'EOF' >assets/keywords_raw.txt
   Gemini 同学 @Gemini 同学
   小爱同学 @小爱同学
   LIGHT UP @LIGHT_UP
   EOF

   sherpa-onnx-cli text2token \
     --tokens assets/tokens.txt \
     --tokens-type phone+ppinyin \
     --lexicon assets/en.phone \
     assets/keywords_raw.txt assets/keywords.txt
   ```
6. 启动程序
   ```shell
   /data/xiaoai-plus/xiaoai_plus_speaker -c /data/xiaoai-plus/config.ini
   ```
   首次部署建议先在 SSH 终端前台运行，确认日志中依次出现：WebSocket 连接成功 → 发送 `setup` → 收到 `setupComplete`，再继续下一步。
7. 设置开机自启动（下载 `boot.sh` 到 `/data/init.sh`）
   ```sh
   curl -L -o /data/init.sh https://fastly.jsdelivr.net/gh/dexterchock/xiaoai-gemini@main/boot.sh
   chmod +x /data/init.sh

   # 重启小爱音箱
   reboot
   ```

## 常见问题

**Gemini 连接失败？** 依次确认：API Key 是否正确、当前网络能否访问 `generativelanguage.googleapis.com`、`config.ini` 中的 `model` 是否为有效的 Live 模型（默认 `gemini-3.8-live`）。

**能启动但没有声音？** 检查本地音频设备、AEC/KWS 是否正常启动、日志中是否收到 `setupComplete` 以及后续的音频数据。

**可以换其他 Gemini Live 模型吗？** 可以，只要该模型支持 Gemini Live API 即可，修改 `config.ini` 中的 `model` 字段。

## 与原项目的关系

本项目基于 [kslr/xiaoai-plus](https://github.com/kslr/xiaoai-plus)（原项目使用 Doubao Realtime API）。主要变化：

- 移除 Doubao 实时协议及相关帧编解码逻辑，改用 Gemini Live WebSocket API 与 JSON 消息格式
- 使用 Google Gemini API Key 进行鉴权，接收 Gemini 原生实时音频
- 保留原项目的音频采集、AEC、KWS、应用与构建架构不变

## 安全提示

请勿将以下信息提交到公开仓库：Gemini API Key、包含真实 Key 的 `config.ini`、音箱 SSH 密码或私钥等敏感凭据。

## 免责声明

- 本项目仅供学习与研究，请确保在合法合规前提下使用。
- 项目与小米、Google/Gemini 官方无从属关系，品牌与商标归属其各自权利人。
- 刷机、修改音箱系统和使用第三方软件可能存在风险，请自行承担相关风险。

## License

MIT

## 致谢

- [kslr/xiaoai-plus](https://github.com/kslr/xiaoai-plus)
- [Open-XiaoAI](https://github.com/idootop/open-xiaoai)
- [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)
- [Google Gemini API](https://ai.google.dev/gemini-api/docs/live-api)
