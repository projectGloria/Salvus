include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(salvus_engine_tests
    ipc_protocol_tests.cpp
    disk_validation_tests.cpp
    disk_reader_tests.cpp
    device_manager_tests.cpp
    ntfs_parser_tests.cpp
    deep_scanner_tests.cpp
    fat32_exfat_partition_tests.cpp
    imaging_integrity_tests.cpp
    corruption_resilience_tests.cpp
)

target_link_libraries(salvus_engine_tests
    PRIVATE
        salvus_engine_core
        GTest::gtest_main
)

if(MSVC)
    target_compile_options(salvus_engine_tests PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()

include(GoogleTest)
gtest_discover_tests(salvus_engine_tests)
