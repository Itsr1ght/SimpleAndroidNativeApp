cmake_minimum_required(VERSION 3.22.1)
project(native_app LANGUAGES C CXX)

# Include native_app_glue DIRECTLY
add_library(native_app SHARED
    main.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

# Include directories
target_include_directories(native_app PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# Link libraries
target_link_libraries(native_app
    android
    log
    EGL
    GLESv3
)