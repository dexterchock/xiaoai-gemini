# xiaoai-gemini

在小爱音箱（OH2P）上获得基于 **Google Gemini Live API** 的端侧实时语音对话体验。

本项目基于 [kslr/xiaoai-plus](https://github.com/kslr/xiaoai-plus) 进行二次开发，保留原项目的音频采集、WebRTC AEC、Sherpa-onnx KWS、应用与部署架构，将 AI 后端替换为 Google 官方 Gemini Live WebSocket API (`BidiGenerateContent`)。

---

## 📌 支持设备

| 型号 | 设备代号 | 架构 |
| --- | --- | --- |
| Xiaomi 智能音箱 Pro | OH2P | ARMv7-A (32-bit armhf) |

> ⚠️ 其他小爱音箱型号未经验证，请勿直接套用。

---

## ✨ 特性

- **Gemini Live 原生双向音频**：直接与 Gemini Live API 连接，支持低延迟实时语音输入与输出。
- **打断与连续对话 (Barge-in)**：支持随时打断 AI 说话，并在一段时间内进行多轮自然交互。
- **纯端侧运行 (No Relay Server)**：程序与全静态依赖库直接运行在音箱 Linux 系统上，无任何中间转接服务器。
- **本地唤醒词 (Sherpa-onnx KWS)**：基于本地 Zipformer 模型实时检测唤醒词，低功耗且响应迅速。
- **远场语音优化**：集成 WebRTC AEC（回声消除）、NS（降噪）与 AGC（自动增益控制），确保播放音乐/AI 说话时仍能清晰拾音。
- **双语自适应 signoff**：支持中文与英文退出指令（如 "bye", "goodbye", "quit", "再见", "拜拜", "退下" 等）。

---

## 🚀 快速开始

### 1. 前置准备
1. **音箱刷机**：刷入 open-xiaoai 补丁固件并开启 SSH 👉 [刷机教程](https://github.com/idootop/open-xiaoai/blob/main/docs/flash.md)
2. **获取 API Key**：前往 [Google AI Studio](https://aistudio.google.com/apikey) 创建 Gemini API Key。

### 2. 一键安装
登录小爱音箱 SSH 终端，运行安装脚本（将解压并安装程序至 `/data/xiaoai-plus`）：

```sh
curl -sSfL [https://fastly.jsdelivr.net/gh/dexterchock/xiaoai-gemini@main/install.sh](https://fastly.jsdelivr.net/gh/dexterchock/xiaoai-gemini@main/install.sh) | sh
