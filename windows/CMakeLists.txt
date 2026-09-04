cmake_minimum_required(VERSION 3.24)
project(kalwer_windows LANGUAGES CXX)

add_executable(kalwer-windows WIN32 kalwer_windows.cpp kalwer.rc)
set_target_properties(kalwer-windows PROPERTIES OUTPUT_NAME kalwer)
target_compile_features(kalwer-windows PRIVATE cxx_std_20)
target_compile_definitions(kalwer-windows PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(kalwer-windows PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(kalwer-windows PRIVATE
    d3d11 d3dcompiler dxgi dcomp d2d1 dwrite windowscodecs
    dwmapi shell32 shlwapi shcore ole32 user32 gdi32 advapi32 winhttp bcrypt)

if(MINGW)
    target_link_options(kalwer-windows PRIVATE -municode -static)
endif()
