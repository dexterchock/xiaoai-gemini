# xiaoai-plus

在小爱音箱上获得与 Gemini 实时语音对话体验


https://github.com/user-attachments/assets/18e65b6b-2a28-415f-be9a-9d7baf11f7cd


## 支持设备

| 型号 | 设备代号 |
| --- | --- |
| Xiaomi 智能音箱 Pro | OH2P |

## 特性

- **实时对话体验**：基于 Gemini Live API 的端到端实时语音，支持实时互动、音色定义、连续对话与随时打断。
- **双助手共存**：小爱同学与 Gemini 同学可同时运行，拥有独立的唤醒词。
- **纯本地运行**：程序完全在音箱本机执行，无需搭建外部中转服务器。
- **自定义唤醒**：支持根据需求自定义关键词进行语音唤醒。
- **远场优化**：集成 AEC（回声消除）、NS（降噪）、AGC（增益），大幅提升远场唤醒与对话的准确率。

## 快速开始

1. 刷机更新小爱音箱补丁固件，开启并 SSH 连接到小爱音箱 👉 [教程](https://github.com/idootop/open-xiaoai/blob/main/docs/flash.md)
2. 创建 Gemini API Key 👉 [Google AI Studio](https://aistudio.google.com/apikey)，并确认当前地区已开启 Gemini Live API
3. 执行安装脚本（会自动下载并安装最新 release 到 `/data/xiaoai-plus`）
   ```sh
   curl -sSfL https://fastly.jsdelivr.net/gh/kslr/xiaoai-plus@main/install.sh | sh
   ```
4. 更新 config.ini 里的配置
   ```ini
   [realtime]
   api_key = your_google_api_key
   model = gemini-3.1-flash-live-preview
   bot_name = Gemini
   system_role = 你是 Gemini 同学，是小爱音箱上的语音助手。
   speaking_style = 语气自然、友好、简洁。
   voice = Kore
   
   [wakeup]
   say_hello = 在
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
7. 设置开机自启动（下载 `boot.sh` 到 `/data/init.sh`）
   ```sh
   curl -L -o /data/init.sh https://fastly.jsdelivr.net/gh/kslr/xiaoai-plus@main/boot.sh
   chmod +x /data/init.sh

   # 重启小爱音箱
   reboot
   ```

## 免责声明

- 本项目仅供学习与研究，请确保在合法合规前提下使用。
- 项目与小米、Google 无从属关系，品牌与商标归属其各自权利人。

## License

MIT

## 致谢

本项目大量参考 https://github.com/idootop/open-xiaoai 研究和开发。
