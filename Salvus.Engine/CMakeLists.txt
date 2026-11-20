cmake_minimum_required(VERSION 3.24)

project(SalvusEngine
    VERSION 1.0.0
    DESCRIPTION "Salvus Native Data Recovery Engine"
    LANGUAGES CXX
)

option(SALVUS_BUILD_TESTS "Build Salvus.Engine unit tests with GoogleTest" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Verify mandatory official branding assets in repository root
set(SALVUS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SALVUS_ICON_ICO "${SALVUS_REPO_ROOT}/Salvus.ico")
set(SALVUS_LOGO_PNG "${SALVUS_REPO_ROOT}/Salvus.png")

if(NOT EXISTS "${SALVUS_ICON_ICO}")
    message(FATAL_ERROR "Mandatory official branding asset missing: ${SALVUS_ICON_ICO}")
endif()

if(NOT EXISTS "${SALVUS_LOGO_PNG}")
    message(FATAL_ERROR "Mandatory official branding asset missing: ${SALVUS_LOGO_PNG}")
endif()

# Core engine library (independently testable without main())
add_library(salvus_engine_core STATIC
    src/common/logger.cpp
    src/common/ipc_protocol.cpp
    src/common/ipc_server.cpp
    src/device/device_manager.cpp
    src/disk/disk_validation.cpp
    src/disk/disk_reader.cpp
    src/filesystem/filesystem_detector.cpp
    src/filesystem/ntfs/ntfs_parser.cpp
    src/filesystem/fat32/fat32_parser.cpp
    src/filesystem/exfat/exfat_parser.cpp
    src/filesystem/refs/refs_parser.cpp
    src/partition/partition_analyzer.cpp
    src/scanner/signature_database.cpp
    src/scanner/quick_scanner.cpp
    src/scanner/deep_scanner.cpp
    src/scanner/scan_manager.cpp
    src/recovery/file_reconstructor.cpp
    src/recovery/recovery_writer.cpp
    src/recovery/integrity_checker.cpp
    src/recovery/recovery_report_generator.cpp
    src/imaging/disk_image_manager.cpp
)

target_include_directories(salvus_engine_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(salvus_engine_core
    PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
)

if(MSVC)
    target_compile_options(salvus_engine_core PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()

if(WIN32)
    target_link_libraries(salvus_engine_core PUBLIC advapi32 setupapi bcrypt)
endif()

# Engine executable (separate process communicating via Named Pipes)
add_executable(Salvus.Engine
    src/main.cpp
    src/app.rc
)

target_link_libraries(Salvus.Engine PRIVATE salvus_engine_core)

if(MSVC)
    target_compile_options(Salvus.Engine PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()

if(SALVUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
