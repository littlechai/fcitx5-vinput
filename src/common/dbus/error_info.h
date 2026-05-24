#pragma once

#include <string>
#include <string_view>

namespace vinput::dbus {

struct ErrorInfo {
  std::string code;
  std::string subject;
  std::string detail;
  std::string raw_message;

  bool empty() const {
    return code.empty() && subject.empty() && detail.empty() &&
           raw_message.empty();
  }
};

constexpr char kErrorInfoSignature[] = "ssss";

constexpr const char *kErrorCodeUnknown = "unknown";
constexpr const char *kErrorCodeDaemonStartFailed = "daemon_start_failed";
constexpr const char *kErrorCodeDaemonRestartFailed = "daemon_restart_failed";
constexpr const char *kErrorCodeDaemonBusy = "daemon_busy";
constexpr const char *kErrorCodeAsrBackendLoading = "asr_backend_loading";
constexpr const char *kErrorCodeAsrBackendReloadFailed =
    "asr_backend_reload_failed";
constexpr const char *kErrorCodeLocalAsrModelCheckFailed =
    "local_asr_model_check_failed";
constexpr const char *kErrorCodeLocalAsrProviderInitFailed =
    "local_asr_provider_init_failed";
constexpr const char *kErrorCodeLocalAsrModelConfigMissing =
    "local_asr_model_config_missing";
constexpr const char *kErrorCodeLocalAsrModelTypeMissing =
    "local_asr_model_type_missing";
constexpr const char *kErrorCodeLocalAsrModelInvalidPath =
    "local_asr_model_invalid_path";
constexpr const char *kErrorCodeLocalAsrModelTokensMissing =
    "local_asr_model_tokens_missing";
constexpr const char *kErrorCodeLocalAsrModelFilesMissing =
    "local_asr_model_files_missing";
constexpr const char *kErrorCodeLocalAsrModelRootResolveFailed =
    "local_asr_model_root_resolve_failed";
constexpr const char *kErrorCodeLocalAsrModelParseFailed =
    "local_asr_model_parse_failed";
constexpr const char *kErrorCodeLocalAsrUnsupportedModelType =
    "local_asr_unsupported_model_type";
constexpr const char *kErrorCodeLocalAsrRecognizerCreateFailed =
    "local_asr_recognizer_create_failed";
constexpr const char *kErrorCodeVadCreateFailed = "vad_create_failed";
constexpr const char *kErrorCodeAudioCaptureLoopNotInitialized =
    "audio_capture_loop_not_initialized";
constexpr const char *kErrorCodePipeWireThreadLoopCreateFailed =
    "pipewire_thread_loop_create_failed";
constexpr const char *kErrorCodePipeWireThreadLoopStartFailed =
    "pipewire_thread_loop_start_failed";
constexpr const char *kErrorCodePipeWirePropertiesAllocFailed =
    "pipewire_properties_alloc_failed";
constexpr const char *kErrorCodePipeWireStreamCreateFailed =
    "pipewire_stream_create_failed";
constexpr const char *kErrorCodePipeWireStreamConnectFailed =
    "pipewire_stream_connect_failed";
constexpr const char *kErrorCodeDbusEventfdCreateFailed =
    "dbus_eventfd_create_failed";
constexpr const char *kErrorCodeDbusUserBusOpenFailed =
    "dbus_user_bus_open_failed";
constexpr const char *kErrorCodeDbusVtableAddFailed = "dbus_vtable_add_failed";
constexpr const char *kErrorCodeDbusNameRequestFailed =
    "dbus_name_request_failed";
constexpr const char *kErrorCodeStartRecordingFailed =
    "start_recording_failed";
constexpr const char *kErrorCodeStartCommandRecordingFailed =
    "start_command_recording_failed";
constexpr const char *kErrorCodeAsrProviderStartFailed =
    "asr_provider_start_failed";
constexpr const char *kErrorCodeAsrProviderTimeout =
    "asr_provider_timeout";
constexpr const char *kErrorCodeAsrProviderFailed =
    "asr_provider_failed";
constexpr const char *kErrorCodeAsrProviderNoText =
    "asr_provider_no_text";
constexpr const char *kErrorCodeLlmRequestFailed =
    "llm_request_failed";
constexpr const char *kErrorCodeLlmHttpFailed =
    "llm_http_failed";
constexpr const char *kErrorCodeProcessingUnknown =
    "processing_unknown";
constexpr const char *kErrorCodePromptFileLoadFailed =
    "prompt_file_load_failed";

ErrorInfo MakeErrorInfo(std::string code, std::string subject = {},
                        std::string detail = {}, std::string raw_message = {});
ErrorInfo MakeRawError(std::string raw_message);
ErrorInfo ClassifyErrorText(std::string_view message);

} // namespace vinput::dbus
