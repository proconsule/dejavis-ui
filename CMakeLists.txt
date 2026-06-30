cmake_minimum_required(VERSION 3.10)
project(dejavis-ui VERSION 0.0.1)

if(WIN32)

set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/dejavis-ui.ico")

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dejavis-ui.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/dejavis-ui.rc
        @ONLY
)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_ARCH "x64")
else()
    set(TARGET_ARCH "x86")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|aarch64")
    set(TARGET_ARCH "arm64")
endif()

set(DIST_DIR "${CMAKE_BINARY_DIR}/dist-${TARGET_ARCH}")



# =============================================================================
# Global Settings
# =============================================================================

set(CMAKE_CXX_STANDARD 20)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_compile_definitions(__STDC_CONSTANT_MACROS)
if(WIN32)
    add_compile_definitions(TYPE_BOOL=1)
    add_compile_options(-D_HAS_STD_BYTE=0)
endif()

if(WIN32 AND MINGW)
    add_definitions(-D__USE_MINGW_ANSI_STDIO=1)
    add_compile_options(-Wno-format)
endif()

option(BUILD_FRONTEND "Compile Frontend (Vite/NPM)" OFF)
option(NDI_SDK_BASE "Compile Frontend (Vite/NPM)" "")


option(DEJAVIS_PROFILING "Enable Tracy profiling" OFF)
set(TRACY_ENABLE ${DEJAVIS_PROFILING} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)

if(NDI_SDK_BASE)
    message(STATUS "NDI SDK Eanbled: adding NDI support")

    add_compile_definitions(NDI_ENABLED)

endif()

if(WIN32 AND MINGW)
    link_directories("${NDI_SDK_BASE}/Lib/x64")
endif()
if(LINUX)
    link_directories("${NDI_SDK_BASE}/lib/x86_64-linux-gnu")
endif()
if(APPLE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    link_directories("${NDI_SDK_BASE}/lib/macOS")
endif()


if(BUILD_FRONTEND)
    message(STATUS "FRONTEND BUILD ENABLED")
else()
    message(STATUS "BUILDING BACKEND ONLY")
endif()


# =============================================================================
# Find System Packages
# =============================================================================

find_package(PkgConfig   REQUIRED)
find_package(OpenGL      REQUIRED)
find_package(Vulkan      REQUIRED)
find_package(Shaderc     REQUIRED)
find_package(glfw3       REQUIRED)
find_package(Drogon      REQUIRED)
find_package(projectM4    REQUIRED)

# --- PortAudio ---
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
message(STATUS "PortAudio ${PORTAUDIO_VERSION} | libs: ${PORTAUDIO_LIBRARIES} | inc: ${PORTAUDIO_INCLUDE_DIRS}")

# --- OpenGL ---
message(STATUS "OpenGL | libs: ${OPENGL_LIBRARIES} | found: ${OPENGL_FOUND}")

# --- Vulkan ---
if(Vulkan_FOUND)
    message(STATUS "Vulkan ${Vulkan_VERSION} | libs: ${Vulkan_LIBRARIES} | inc: ${Vulkan_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Vulkan NOT found!")
endif()

# --- Shaderc ---
if(Shaderc_FOUND)
    get_target_property(SHADERC_LIB shaderc::shaderc LOCATION)
    message(STATUS "Shaderc found | lib: ${SHADERC_LIB}")
endif()

# --- GLFW3 ---
if(glfw3_FOUND)
    get_target_property(GLFW_LIB glfw LOCATION)
    message(STATUS "GLFW3 ${glfw3_VERSION} | lib: ${GLFW_LIB}")
endif()

pkg_check_modules(EGL egl)

# --- FFmpeg (via pkg-config) ---
if(APPLE)
    set(FFMPEG_FULL_PATH "/opt/homebrew/opt/ffmpeg-full")
    set(ENV{PKG_CONFIG_PATH} "${FFMPEG_FULL_PATH}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    message(STATUS "FFmpeg-full path: ${FFMPEG_FULL_PATH}")
endif()

pkg_check_modules(AVCODEC    REQUIRED libavcodec)
pkg_check_modules(AVFORMAT   REQUIRED libavformat)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
pkg_check_modules(AVFILTER   REQUIRED libavfilter)
pkg_check_modules(AVUTIL     REQUIRED libavutil)
pkg_check_modules(SWSCALE    REQUIRED libswscale)

if(AVCODEC_FOUND AND AVFORMAT_FOUND AND SWRESAMPLE_FOUND AND AVUTIL_FOUND)
    message(STATUS "FFmpeg | avcodec: ${AVCODEC_VERSION} | avformat: ${AVFORMAT_VERSION} | swresample: ${SWRESAMPLE_VERSION} | avutil: ${AVUTIL_VERSION}")
endif()

set(FFMPEG_INCLUDE_DIRS
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
)
message(STATUS "FFMPEG_INCLUDE_DIRS: ${FFMPEG_INCLUDE_DIRS}")

# --- Drogon ---
if(Drogon_FOUND)
    message(STATUS "Drogon ${Drogon_VERSION} | target: Drogon::Drogon")
endif()


# =============================================================================
# FetchContent Dependencies
# =============================================================================

include(FetchContent)

# --- ImGui ---
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        6ded523
        GIT_SHALLOW    TRUE
)
message(STATUS "Fetching ImGui...")
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
message(STATUS "Linking ImGui...")
target_link_libraries(imgui PUBLIC Vulkan::Vulkan)

# --- SQLiteCpp ---
FetchContent_Declare(
        SQLiteCpp
        GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
        GIT_TAG        3.3.3
        GIT_SHALLOW    TRUE
)
set(SQLITECPP_INTERNAL_SQLITE ON  CACHE BOOL "" FORCE)
set(SQLITECPP_RUN_CPPLINT     OFF CACHE BOOL "" FORCE)
message(STATUS "Fetching SQLiteCpp...")
FetchContent_MakeAvailable(SQLiteCpp)

# --- libdatachannel ---
FetchContent_Declare(
    datachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.24.1
    GIT_SHALLOW    TRUE
)
set(USE_GNUTLS  OFF CACHE BOOL "" FORCE)
set(USE_NICE    OFF CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON  CACHE BOOL "" FORCE)
set(NO_TESTS    ON  CACHE BOOL "" FORCE)
message(STATUS "Fetching libdatachannel...")
FetchContent_MakeAvailable(datachannel)

include(FetchContent)

FetchContent_Declare (
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.13.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable ( tracy )

# =============================================================================
# SSL Certificate Generation
# =============================================================================

set(CERT_DIR  "${CMAKE_CURRENT_BINARY_DIR}")
set(CERT_FILE "${CERT_DIR}/server.crt")
set(KEY_FILE  "${CERT_DIR}/server.key")

if(WIN32)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cert_config.txt"
        "[req]\n"
        "distinguished_name = req_distinguished_name\n"
        "prompt = no\n"
        "[req_distinguished_name]\n"
        "C = IT\n"
        "ST = State\n"
        "L = City\n"
        "O = Dejavis\n"
        "OU = Dev\n"
        "CN = localhost\n"
    )
    add_custom_command(
        OUTPUT  "${CERT_FILE}" "${KEY_FILE}"
        COMMAND openssl req -x509 -newkey rsa:4096
                -keyout "${KEY_FILE}"
                -out    "${CERT_FILE}"
                -days 365 -nodes
                -config "${CMAKE_CURRENT_BINARY_DIR}/cert_config.txt"
        COMMENT "Generating SSL certificates (Windows)..."
        VERBATIM
    )
else()
    # macOS and Linux share the same openssl command
    add_custom_command(
        OUTPUT  ${CERT_FILE} ${KEY_FILE}
        COMMAND openssl req -x509 -newkey rsa:4096
                -keyout ${KEY_FILE}
                -out    ${CERT_FILE}
                -days 365 -nodes
                -subj "/C=IT/ST=State/L=City/O=Dejavis/OU=Dev/CN=localhost"
        COMMENT "Generating SSL certificates..."
    )
endif()

add_custom_target(generate_certs DEPENDS ${CERT_FILE} ${KEY_FILE})


# =============================================================================
# Common Source Files
# =============================================================================

set(DEJAVISUI_COMMON_SOURCES
        backend/src/userconfig.cpp
        backend/src/main.cpp
        backend/src/webcontroller/websocket.cpp
        backend/src/webcontroller/uploadmanager.cpp
        backend/src/webcontroller/webrtc_broadcaster.cpp
        backend/src/webcontroller/webrtc_session.cpp
        backend/src/audio/audio.cpp
        backend/src/audio/audio_mixer.cpp
        backend/src/audio/audio_rtc.cpp
        backend/src/audio/audio_resampler.cpp
        backend/src/audio/audio_dev.cpp
        backend/src/audioplayer/audioplayer.cpp
        backend/src/db/milkplaylistdb.cpp
        backend/src/fs/localfilebrowser.cpp
        backend/src/video/renderer_core.cpp
        backend/src/video/renderer_utils.cpp
        backend/src/video/renderer_display.cpp
        backend/src/video/renderer_glfw.cpp
        backend/src/video/renderer_imgui.cpp
        backend/src/video/renderer_videomixer.cpp
        backend/src/av_encoder.cpp
        backend/src/av_decoder.cpp
        backend/src/image_viewer.cpp
        backend/src/video/videofx_pipeline.cpp
        backend/src/video/rgb2yuv_pipeline.cpp
        backend/src/ndi_receiver.cpp
        backend/src/ndi_sender.cpp
        backend/src/video/postprocess.cpp
        backend/src/video/vulkan_utils.cpp
        backend/src/video/projectm_wrapper.cpp
        backend/src/video/yuv2rgb/YUV2RGBPipeline.cpp
        backend/src/video/yuv2rgb/YUVConverter_Common.cpp
        backend/src/video/yuv2rgb/converters/UYVYConverter.cpp
        backend/src/video/yuv2rgb/converters/YUV420PConverter.cpp
        backend/src/video/yuv2rgb/converters/NV12Converter.cpp
        backend/src/video/yuv2rgb/converters/P010Converter.cpp
        backend/src/unimixer.cpp
        backend/src/audio/audioeffects/audioeffects.cpp
        backend/src/audio/audioeffects/EffectBank.cpp
        backend/src/audio/audioeffects/FFmpegLimiter.cpp
        backend/src/audio/audioeffects/FFmpegEcho.cpp
        backend/src/audio/audioeffects/FFmpegChorus.cpp
        backend/src/audio/audioeffects/FFmpegTremolo.cpp
        backend/src/audio/audioeffects/FFmpegVibrato.cpp

)


# =============================================================================
# Executable Target
# =============================================================================

message(STATUS "OPERATING SYSTEM: ${CMAKE_SYSTEM_NAME}")

if(APPLE)
    set(DEJAVISUI_FONTS assets/font.ttf)

    set(MAC_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.icns")

    set_source_files_properties(${MAC_ICON_FILE} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
    )

    add_executable(dejavis-ui MACOSX_BUNDLE
            ${DEJAVISUI_COMMON_SOURCES}
            backend/src/fs/folders_mac.mm
            backend/src/audio/macos_permissions.mm
            ${DEJAVISUI_FONTS}
            ${CERT_FILE}
            ${KEY_FILE}
            ${MAC_ICON_FILE}
    )


    set_source_files_properties(${DEJAVISUI_FONTS} ${CERT_FILE} ${KEY_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )


    set_target_properties(dejavis-ui PROPERTIES
            MACOSX_BUNDLE             TRUE
            MACOSX_BUNDLE_INFO_PLIST  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
    )

    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
    add_dependencies(dejavis-ui generate_certs)

else()
    if(WIN32)
        add_executable(dejavis-ui
            ${DEJAVISUI_COMMON_SOURCES}
            ${CERT_FILE}
            ${KEY_FILE}
            ${CMAKE_CURRENT_BINARY_DIR}/dejavis-ui.rc
        )
    else()
        add_executable(dejavis-ui
                ${DEJAVISUI_COMMON_SOURCES}
                ${CERT_FILE}
                ${KEY_FILE}

        )
    endif()
    # Copy font to build directory on Windows/Linux
    add_custom_command(TARGET dejavis-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/font.ttf"
                "$<TARGET_FILE_DIR:dejavis-ui>/font.ttf"
        COMMENT "Copying font.ttf to build directory..."
    )
endif()


# =============================================================================
# GLAD (OpenGL Loader)
# =============================================================================

if(APPLE)
    message(STATUS "Using GLAD for macOS")
    target_sources(dejavis-ui PRIVATE backend/src/glad_macos/src/glad.c)
else()
    message(STATUS "Using GLAD with OpenGL 4.6")
    target_sources(dejavis-ui PRIVATE backend/src/glad/src/glad.c)
endif()

target_include_directories(dejavis-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/glad/include
)


# =============================================================================
# Include Directories
# =============================================================================


target_include_directories(dejavis-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${FFMPEG_INCLUDE_DIRS}
    ${datachannel_SOURCE_DIR}/include
    ${datachannel_BINARY_DIR}/include
        ${NDI_SDK_BASE}/include
)


if(EGL_FOUND)
    target_link_libraries(dejavis-ui PRIVATE ${EGL_LIBRARIES})
    target_include_directories(dejavis-ui PRIVATE ${EGL_INCLUDE_DIRS})
    add_definitions(-DHAVE_EGL)

    # Se EGL::EGL non è disponibile (versioni vecchie di CMake), usa:
    # target_include_directories(mia_app PRIVATE ${EGL_INCLUDE_DIRS})
    # target_link_libraries(mia_app PRIVATE ${EGL_LIBRARIES})
endif()

# =============================================================================
# Link Libraries
# =============================================================================

if(WIN32)
target_link_libraries(dejavis-ui PRIVATE Processing.NDI.Lib.x64)
target_include_directories(dejavis-ui PRIVATE "${NDI_SDK_BASE}/Include")
endif ()
if(LINUX)
    target_link_libraries(dejavis-ui PRIVATE ndi)
    target_include_directories(dejavis-ui PRIVATE "${NDI_SDK_BASE}/include")
endif()
if(APPLE)
    target_link_libraries(dejavis-ui PRIVATE ndi)
    target_include_directories(dejavis-ui PRIVATE "${NDI_SDK_BASE}/include")
endif()
# Core
target_link_libraries(dejavis-ui PRIVATE
    imgui
    Vulkan::Vulkan
    shaderc::shaderc
    SPIRV-Tools-opt
    SPIRV-Tools
    glslang
    glfw
    OpenGL::GL
    Drogon::Drogon
    SQLiteCpp
    datachannel-static
    libprojectM::projectM
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)


target_link_directories(dejavis-ui PRIVATE ${AVCODEC_LIBRARY_DIRS})

target_link_libraries (dejavis-ui PUBLIC TracyClient )

if(WIN32)
    target_link_libraries(dejavis-ui PRIVATE winmm)
endif()

# PortAudio (platform-specific)
if(APPLE)
    link_directories(/opt/homebrew/lib)
    include_directories(/opt/homebrew/include)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/lib")

    find_library(PORTAUDIO_LIB NAMES portaudio PATHS /opt/homebrew/lib)
    target_link_libraries(dejavis-ui PRIVATE
        ${PORTAUDIO_LIB}
        ${PORTAUDIO_LIBRARIES}
            "-framework AVFoundation"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreFoundation"
            "-framework CoreServices"
            "-framework CoreAudio"
            "-framework AppKit"
    )
    message(STATUS "Linking macOS frameworks")
else()
    target_link_libraries(dejavis-ui PRIVATE ${PORTAUDIO_LIBRARIES})
endif()

# Spout2 (Windows only, optional)
set(SPOUT2_SDK_PATH "" CACHE PATH "Path to Spout2 SDK root folder")

if(SPOUT2_SDK_PATH AND WIN32)
    set(spout2_DIR "${SPOUT2_SDK_PATH}/")
    find_package(spout2 REQUIRED)

    add_definitions(-DSPOUT_IMPORT_DLL)
    target_compile_definitions(dejavis-ui PRIVATE USE_SPOUT)
    target_link_libraries(dejavis-ui PRIVATE Spout2::SpoutDX)
    target_sources(dejavis-ui PRIVATE
        backend/src/SpoutVK.cpp
        backend/src/video/renderer_spout2.cpp
    )
    add_compile_definitions(SPOUT2_ENABLED)
    message(STATUS "Spout2 SDK found: ${SPOUT2_SDK_PATH}")
endif()


# =============================================================================
# Compile Definitions
# =============================================================================

target_compile_definitions(dejavis-ui PRIVATE UI_PATH="${UI_DIST_DIR}")


# =============================================================================
# Frontend (optional Vite/NPM build)
# =============================================================================

if(BUILD_FRONTEND)

    set(FRONTEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend")
    set(UI_DIST_DIR  "${CMAKE_CURRENT_BINARY_DIR}/webpages")

    if(WIN32)
        find_program(CMD_EXE         NAMES cmd.exe)
        find_program(NPM_EXECUTABLE  NAMES npm.cmd npm
                PATHS "$ENV{ProgramFiles}/nodejs"
                NO_DEFAULT_PATH)
        message(STATUS "Found npm: ${NPM_EXECUTABLE}, cmd: ${CMD_EXE}")

        set(NODE_PATH        "C:/Program Files/nodejs")
        set(CLEAN_NPM_COMMAND "$ENV{ProgramFiles}\\nodejs\\npm.cmd")
        message(STATUS "Using npm command: ${CLEAN_NPM_COMMAND}")
    else()
        find_program(NPM_EXECUTABLE NAMES npm)
        if(NPM_EXECUTABLE)
            message(STATUS "npm found: ${NPM_EXECUTABLE}")
        else()
            message(FATAL_ERROR "npm not found or not in PATH")
        endif()
    endif()

    # npm install
    if(WIN32)
        add_custom_command(
                OUTPUT  "${FRONTEND_DIR}/node_modules/.install_stamp"
                COMMAND ${CMAKE_COMMAND} -E env "PATH=${NODE_PATH};$ENV{PATH}" "${CLEAN_NPM_COMMAND}" install
                COMMAND ${CMAKE_COMMAND} -E touch "${FRONTEND_DIR}/node_modules/.install_stamp"
                WORKING_DIRECTORY ${FRONTEND_DIR}
                COMMENT "Installing frontend dependencies..."
        )
    else()
        add_custom_command(
                OUTPUT  "${FRONTEND_DIR}/node_modules/.install_stamp"
                COMMAND ${NPM_EXECUTABLE} install
                COMMAND ${CMAKE_COMMAND} -E touch "${FRONTEND_DIR}/node_modules/.install_stamp"
                WORKING_DIRECTORY ${FRONTEND_DIR}
                COMMENT "Installing frontend dependencies..."
        )
    endif()

    # npm run build
    if(WIN32)
        add_custom_command(
                OUTPUT  "${UI_DIST_DIR}/index.html"
                COMMAND ${CMAKE_COMMAND} -E env "PATH=${NODE_PATH};$ENV{PATH}"
                "${NPM_EXECUTABLE}" run build -- --outDir "${UI_DIST_DIR}" --emptyOutDir
                DEPENDS "${FRONTEND_DIR}/node_modules/.install_stamp"
                WORKING_DIRECTORY ${FRONTEND_DIR}
                COMMENT "Building Vite frontend -> ${UI_DIST_DIR}..."
        )
    else()
        add_custom_command(
                OUTPUT  "${UI_DIST_DIR}/index.html"
                COMMAND ${NPM_EXECUTABLE} run build -- --outDir "${UI_DIST_DIR}" --emptyOutDir
                DEPENDS "${FRONTEND_DIR}/node_modules/.install_stamp"
                WORKING_DIRECTORY ${FRONTEND_DIR}
                COMMENT "Building Vite frontend -> ${UI_DIST_DIR}..."
        )
    endif()

    add_custom_target(frontend_target ALL DEPENDS "${UI_DIST_DIR}/index.html")
    add_dependencies(dejavis-ui frontend_target)

endif()

# =============================================================================
# macOS Bundle Post-Build
# =============================================================================

if(APPLE)
    set_target_properties(dejavis-ui PROPERTIES
            MACOSX_BUNDLE       TRUE
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH       "@executable_path/../Frameworks"
    )

    # Usa il generator expression per passare il path corretto a FixmacOSBundle.cmake
    add_custom_command(TARGET dejavis-ui POST_BUILD
            COMMAND ${CMAKE_COMMAND}
            -DBUNDLE_DIR=$<TARGET_BUNDLE_DIR:dejavis-ui>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FixmacOSBundle.cmake"
            COMMENT "Fixing up macOS bundle dependencies..."
    )

    if(BUILD_FRONTEND)

        add_custom_command(TARGET dejavis-ui POST_BUILD
                # 1. Assicurati che la cartella Resources esista dentro il bundle
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:dejavis-ui>/Contents/Resources"

                # 2. Copia la directory (usa le virgolette per i path con spazi)
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${UI_DIST_DIR}"
                "$<TARGET_BUNDLE_DIR:dejavis-ui>/Contents/Resources/frontend"

                COMMENT "copying frontend files into bundle macOS..."
                VERBATIM
        )
    endif ()

    # Firma finale (deve restare per ultima)
    set(ENTITLEMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/entitlements.plist")
    add_custom_command(TARGET dejavis-ui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            ${ENTITLEMENTS_FILE}
            ${CMAKE_CURRENT_BINARY_DIR}/entitlements.plist
            COMMAND codesign --force --deep --sign -
            --entitlements ${CMAKE_CURRENT_BINARY_DIR}/entitlements.plist
            $<TARGET_BUNDLE_DIR:dejavis-ui>
            COMMENT "Ad-hoc signing with entitlements..."
            VERBATIM
    )
endif()


if(WIN32)

    add_custom_command(
            TARGET dejavis-ui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/dejavis-ui.exe"
            "${DIST_DIR}/dejavis-ui.exe"
            COMMENT "Copy dejavis-ui to dist directory"
    )

elseif (LINUX)
    add_custom_command(
            TARGET dejavis-ui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/dejavis-ui"
            "${DIST_DIR}/dejavis-ui"
            COMMENT "Copy dejavis-ui to ${DIST_DIR} directory"
    )

endif ()

add_custom_command(
        TARGET dejavis-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/font.ttf"
        "${DIST_DIR}/font.ttf"
        COMMENT "Copy to ${DIST_DIR} directory\n"
)

add_custom_command(
        TARGET dejavis-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/server.key"
        "${DIST_DIR}/server.key"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/server.crt"
        "${DIST_DIR}/server.crt"
        COMMENT "Copy cert files to ${DIST_DIR}\n"
)

add_custom_command(
        TARGET dejavis-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/webpages"
        "${DIST_DIR}/webpages"
        COMMENT "Copy web pages to ${DIST_DIR}/webpages directory\n"
)