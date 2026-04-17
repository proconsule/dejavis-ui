## DEJAVISUI

DEJAVISUI is a trivial Audio/Video mixer with VIS

Features:

- Audio Mixer (16x2) Input can be live,url,file
- Video Mixer
- projectM vis (openGL -> Vulkan zero-copy)
- SRT output
- SPOUT2 support (windows only)
- Web Interface for setting/controlling
- Win/Lin/Mac support


Since this is a complex software involving many process/protocols/codecs it uses many great library under the hood

Library used:

- Drogon (for HTTPS/WebSocket) https://github.com/drogonframework/drogon
- ffmpeg (Audio/Video Codec, Formats, Streaming )https://ffmpeg.org/
- projectM (Visualizer) https://projectm.io
- portaudio (Audio I/O multiplatform) https://portaudio.com/
- SQLiteC++ https://github.com/srombauts/sqlitecpp
- shaderc https://github.com/google/shaderc
- SPIR-V Tools https://github.com/khronosGroup/SPIRV-Tools
- SPOUT2 (Windows only) https://github.com/leadedge/Spout2
- DaisySP (Audio DSP)https://github.com/electro-smith/DaisySP

