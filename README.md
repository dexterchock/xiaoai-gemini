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
* **Local Wake Word**: Say **"Hey Gemini"**. Detected offline by a Sherpa-onnx Zipformer model.
* **Any Language**: Gemini replies in whatever language you speak to it and follows you if you switch. See `language_instruction` in the config.
* **Far-field Processing**: WebRTC AEC, NS, and AGC integrated for echo cancellation and audio enhancement.
* **Bilingual Signoff**: Detects English and Chinese exit phrases (e.g., "bye", "that's all", "再见", "拜拜").
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
# Optional. Appended to the system prompt. The default tells Gemini to reply in the user's
# language. Set it to an empty value to turn it off. Do not use # or ; inside the value.
# language_instruction =

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

The shipped wake word is **"Hey Gemini"** (`assets/keywords.txt`). To change it, generate new token lines on your host machine (not on the speaker). English words must be **UPPERCASE**, because the lexicon is uppercase.

```bash
python3 -m pip install -U sherpa-onnx click sentencepiece pypinyin

# tokens.txt and the English lexicon (en.phone) ship inside the keyword-spotting model:
curl -SLO https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2
tar xf sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2

printf 'HEY GEMINI @Hey_Gemini\n' > keywords_raw.txt

sherpa-onnx-cli text2token \
  --tokens sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt \
  --tokens-type phone+ppinyin \
  --lexicon sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/en.phone \
  keywords_raw.txt assets/keywords.txt
```

Chinese phrases work too (for example `小爱同学 @小爱同学`). Use one `@label` per wake word. Several lines may share a label to cover alternate pronunciations.

To try a new keyword file without rebuilding, copy it to `/data/xiaoai-plus/assets/keywords.txt` on the speaker and restart the app.

## License

[MIT](https://www.google.com/search?q=LICENSE&utm_source=gemini)
