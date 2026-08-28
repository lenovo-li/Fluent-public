cmake_minimum_required(VERSION 3.23)
project(FluentUIPackageConsumer LANGUAGES CXX)

find_package(FluentUI CONFIG REQUIRED)
add_executable(FluentUIPackageConsumer WIN32
    ../Minimal/main.cpp
    ../Minimal/app.rc)
target_compile_features(FluentUIPackageConsumer PRIVATE cxx_std_20)
target_link_libraries(FluentUIPackageConsumer PRIVATE FluentUI::FluentUI)
if(MSVC)
    set_property(TARGET FluentUIPackageConsumer PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
