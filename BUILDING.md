## BUILDING

To build the project, follow these steps:

what you need:

- working build env
- cmake (should be present on your package manager)
- vulkan lib (should be present on your package manager)
- shaderc https://github.com/google/shaderc (should be present on your package manager)
- GLFW https://github.com/glfw/glfw (should be present on your package manager)
- portuadio https://portaudio.com/ (should be present on your package manager)
- ffmpeg (should be present on your package manager)
- projectM https://github.com/projectM-visualizer/projectm even if is present on your package manager i strongly suggest to compile it from source
- drogon https://github.com/drogonframework/drogon even if is present on your package manager i strongly suggest to compile it from source
- node && npm https://nodejs.org/ (for frontend building)
- spout2 sdk (optional, win only) https://github.com/leadedge/Spout2
- NDI sdk https://ndi.video/for-developers/ndi-sdk/

### Windows

the build can be done using https://www.msys2.org/ using mingw (probably it should compile well also with VS but i dont't like their build system so is untested)

use pacman to fetch requeried libraries
```
mkdir build
cd build
cmake ../ -DBUILD_FRONTEND=ON (-DSPOUT2_SDK_PATH=path for spout2) -DDNDI_SDK_BASE=path for ndi sdk
cmake --build .
```

### Linux

install requeried libraries with the system package manager
```
mkdir build
cd build
cmake ../ -DBUILD_FRONTEND=ON -DDNDI_SDK_BASE=path for ndi sdk
cmake --build .
```

### MacOS

install requeried libraries using brew https://brew.sh/

(use ffmpeg-full instead of ffmpeg)
```
mkdir build
cd build
cmake ../ -DBUILD_FRONTEND=ON -DDNDI_SDK_BASE=path for ndi sdk
cmake --build .
```
----
#### Running the software:

start the resulting executable it will create a default config file and it will start a webserver on port 8848

Edit the config file to setup resolution, file paths and so on.

On MacOS the path is in Library/Application Support/ , is a standard for this OS