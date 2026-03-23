option(VINPUT_FETCH_CLI11 "Fetch CLI11 with FetchContent when not found" ON)

find_package(CLI11 QUIET CONFIG)
if(NOT CLI11_FOUND)
    if(VINPUT_FETCH_CLI11)
        include(FetchContent)
        FetchContent_Declare(
            CLI11
            GIT_REPOSITORY https://github.com/CLIUtils/CLI11
            GIT_TAG v2.4.2
        )
        FetchContent_MakeAvailable(CLI11)
    else()
        message(FATAL_ERROR
            "CLI11 was not found. Install CLI11 or enable VINPUT_FETCH_CLI11.")
    endif()
endif()

pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

add_executable(vinput
    main.cpp
    formatter.cpp
    progress.cpp
    editor_utils.cpp
    command_init.cpp
    command_model.cpp
    command_scene.cpp
    command_config.cpp
    command_llm.cpp
    command_asr.cpp
    command_hotword.cpp
    command_device.cpp
    command_daemon.cpp
    command_status.cpp
    command_recording.cpp
    command_adaptor.cpp
    dbus_client.cpp
    systemd_client.cpp
    ${CMAKE_SOURCE_DIR}/src/common/pipewire_device.cpp
)

target_include_directories(vinput PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PIPEWIRE_INCLUDE_DIRS}
)

target_link_libraries(vinput PRIVATE
    vinput-common
    CLI11::CLI11
    CURL::libcurl
    ${SDBUS_LIBRARIES}
    LibArchive::LibArchive
    ${PIPEWIRE_LIBRARIES}
)

install(TARGETS vinput
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/data/asr-providers/elevenlabs_speech_to_text.py
    ${CMAKE_SOURCE_DIR}/data/asr-providers/mlx_audio_speech_to_text.py
    DESTINATION ${VINPUT_ASR_PROVIDER_DIR}
)

install(PROGRAMS
    ${CMAKE_SOURCE_DIR}/data/llm-adaptors/mtranserver_proxy.py
    DESTINATION ${VINPUT_LLM_ADAPTOR_DIR}
)
