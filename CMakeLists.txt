cmake_minimum_required(VERSION 3.16)
project(dns_resolver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖项
find_package(c-ares REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(prometheus-cpp REQUIRED)

option(ENABLE_TESTS "Enable unit tests" OFF)
option(ENABLE_EXAMPLE "Enable example" ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")

# 添加源文件
add_library(dns_resolver STATIC
        src/DNSCache.cpp
        src/DNSResolver.cpp
        src/DNSCachePersistor.cpp
        src/DNSConfig.cpp
        src/DNSConfigValidator.cpp
        src/DNSConfigVersion.cpp
        src/DNSEvent.cpp
        src/DNSMetrics.cpp
)

if (WIN32)
    target_compile_definitions(dns_resolver PUBLIC -DNOMINMAX)
endif ()

if (MSVC)
    set_property(TARGET dns_resolver PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

target_include_directories(dns_resolver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 链接c-ares
target_link_libraries(dns_resolver
        PUBLIC
        c-ares::cares
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        prometheus-cpp::core
        prometheus-cpp::pull
        prometheus-cpp::util
)

if (ENABLE_EXAMPLE)
    add_subdirectory(examples)
endif ()

if (ENABLE_TESTS)
    #    find_package(GTest REQUIRED)
    #    add_subdirectory(unit_tests)
endif ()
