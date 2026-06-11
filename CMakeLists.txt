cmake_minimum_required(VERSION 3.16)

project(vr_offset_animation LANGUAGES CXX)

add_executable(vr-offset-animation
    src/main.cpp
)

target_compile_features(vr-offset-animation PRIVATE cxx_std_17)

target_include_directories(vr-offset-animation PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../third-party/openvr/headers
)

if(WIN32)
    target_link_libraries(vr-offset-animation PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/openvr/lib/win64/openvr_api.lib
        ws2_32
    )
elseif(APPLE)
    target_link_libraries(vr-offset-animation PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/openvr/lib/osx32/openvr_api.dylib
    )
else()
    target_link_libraries(vr-offset-animation PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../third-party/openvr/lib/linux64/libopenvr_api.so
    )
endif()
