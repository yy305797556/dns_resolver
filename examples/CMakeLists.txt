

add_executable(config_example
        ConfigExample.cpp
)

target_link_libraries(config_example
        PRIVATE
        dns_resolver
)

if (MSVC)
    set_property(TARGET config_example PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

add_executable(batch_dns_resolver
        BatchDNSResolver.cpp
)

target_link_libraries(batch_dns_resolver
        PRIVATE
        dns_resolver
)

if (MSVC)
    set_property(TARGET batch_dns_resolver PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()