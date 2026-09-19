# xiaoai-gemini

Google Gemini Live API integration running directly on Xiaomi Smart Speaker (OH2P).

This project is built upon `kslr/xiaoai-plus`, retaining its audio capture, WebRTC AEC, Sherpa-onnx KWS, and deployment architecture, while replacing the backend with Google Gemini Live WebSocket API (`BidiGenerateContent`).

## Supported Hardware

| Model | Device Code | Architecture |
| --- | --- | --- |
| Xiaomi Smart Speaker Pro | OH2P | ARMv7-A (32-bit armhf) |

## Features

* **Gemini Live Bidi-Audio**: Direct WebSocket connection to Gemini Live API for low-latency voice interaction.
* **Barge-in & Interruption**: Supports interruption during AI speech output.
* **On-Device Execution**: Runs standalone on the device without relay servers.
* **Local Wake Word**: Sherpa-onnx Zipformer model for offline keyword detection.
* **Far-field Processing**: WebRTC AEC, NS, and AGC integrated for echo cancellation and audio enhancement.
* **Bilingual Signoff**: Auto-detects English and Chinese exit phrases (e.g., "bye", "exit", "quit", "再见", "拜拜").
* **Google Search grounding**: Built-in real-time web search for live queries such as local weather, breaking news, and current events.

## Quick Start

### 1. Prerequisites

* SSH access enabled on Xiaomi Speaker via open-xiaoai patch firmware.
* Gemini API key from Google AI Studio.

### 2. Installation

Run the installer on the speaker terminal:

```sh
curl -sSfL https://fastly.jsdelivr.net/gh/dexterchock/xiaoai-gemini@main/install.sh | sh

```

### 3. Configuration

Edit `/data/xiaoai-plus/config.ini` to match your credentials:

```ini
[realtime]
api_key = your_google_api_key
model = gemini-3.8-live
bot_name = Google
system_role = You are an articulate, highly capable, and poised AI assistant running on a smart speaker. Deliver answers directly with zero operational fluff or robotic greetings, formatted naturally for voice playback.
speaking_style = Speak naturally in a calm, polished, and executive tone. Keep responses short, elegant, and precise.
voice = Leda
google_search = true

[wakeup]
say_hello = Yes?

[audio]
playback_gain = 1.0

```

### 4. Run Executable

Test running the binary in the foreground:

```sh
/data/xiaoai-plus/xiaoai_plus_speaker -c /data/xiaoai-plus/config.ini

```

### 5. Enable Autostart on Boot

Set up `/data/init.sh` for boot-time execution:

```sh
curl -L -o /data/init.sh https://fastly.jsdelivr.net/gh/dexterchock/xiaoai-gemini@main/boot.sh
chmod +x /data/init.sh
reboot

```

## Custom Wake Keywords

To generate custom keywords, run the following on your host machine (not on the speaker):

```bash
python3 -m pip install -U sherpa-onnx click sentencepiece pypinyin

cat <<'EOF' > assets/keywords_raw.txt
Gemini 同学 @Gemini 同学
小爱同学 @小爱同学
EOF

sherpa-onnx-cli text2token \
  --tokens assets/tokens.txt \
  --tokens-type phone+ppinyin \
  --lexicon assets/en.phone \
  assets/keywords_raw.txt assets/keywords.txt

```

Transfer the resulting `keywords.txt` to `/data/xiaoai-plus/assets/keywords.txt`.

## License

[MIT](https://www.google.com/search?q=LICENSE&utm_source=gemini)
