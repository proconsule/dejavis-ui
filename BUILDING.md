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


### Windows

the build can be done using https://www.msys2.org/ using mingw

use pacman to fetch requeried libraries
```
mkdir build
cd build
cmake ../ -DBUILD_FRONTEND=ON (-DSPOUT2_SDK_PATH=path for spout2)
cmake --build .
```

### Linux

install requeried libraries with the system package manager
```
mkdir build
cd build
cmake ../ -DBUILD_FRONTEND=ON
cmake --build .
```

### MacOS

install requeried libraries using brew https://brew.sh/

(use ffmpeg-full instead of ffmpeg)
```
mkdir build
cd build
cmake ../ -DBUILD_FRONTEND=ON
cmake --build .
```