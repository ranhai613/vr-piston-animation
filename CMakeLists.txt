cmake_minimum_required(VERSION 3.16)

project(vr_piston_animation LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "vr-piston-animation can only be built on Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "vr-piston-animation requires a 64-bit Windows build.")
endif()

add_executable(vr-piston-animation WIN32
    src/main.cpp
    src/steamvr_registration.cpp
    src/tray_icon.cpp
    src/windows_entry.cpp
)

target_compile_features(vr-piston-animation PRIVATE cxx_std_17)

target_include_directories(vr-piston-animation PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/third-party/openvr/headers
    ${CMAKE_CURRENT_LIST_DIR}/third-party/nlhomann
)

target_link_libraries(vr-piston-animation PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/third-party/openvr/lib/win64/openvr_api.lib
    ws2_32
    shell32
    user32
)

if(MINGW)
    target_link_options(vr-piston-animation PRIVATE -static -municode)
endif()

add_custom_command(TARGET vr-piston-animation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_LIST_DIR}/third-party/openvr/bin/win64/openvr_api.dll
        $<TARGET_FILE_DIR:vr-piston-animation>/openvr_api.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_LIST_DIR}/steamvr/manifest.vrmanifest
        $<TARGET_FILE_DIR:vr-piston-animation>/manifest.vrmanifest
)
