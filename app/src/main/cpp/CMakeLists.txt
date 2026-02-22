cmake_minimum_required(VERSION 3.22)
project(service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(service service.cpp)

target_compile_options(service PRIVATE
        -O2
        -fvisibility=hidden
        -DNDEBUG
)

target_link_libraries(service PRIVATE
        android
        log
        m
        z
        dl
)

# 输出到 jniLibs/${ANDROID_ABI}
set_target_properties(service PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}"
)

# 构建完成后复制为 libservice.so
add_custom_command(TARGET service POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:service>
        "${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libservice.so"
)