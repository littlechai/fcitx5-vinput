#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct CoreConfig;

namespace vinput::daemon::remote {

class RemoteTextService {
public:
  RemoteTextService() = default;
  ~RemoteTextService();

  RemoteTextService(const RemoteTextService &) = delete;
  RemoteTextService &operator=(const RemoteTextService &) = delete;

  bool Synchronize(const CoreConfig &config, std::string *error);
  void Shutdown();

private:
  struct Settings {
    int port = 8080;
    int debounce_ms = 1500;
    std::string api_key;
  };

  bool ExtractSettings(const CoreConfig &config, Settings *settings,
                       bool *should_run, std::string *error) const;
  bool Start(const Settings &settings, std::string *error);
  bool OpenListenSocket(const Settings &settings, std::string *error);
  void AcceptLoop();
  void HandleClient(int fd);
  void HandleHttpRequest(int fd, const std::string &request);
  void HandleRealtime(int fd, bool peer_is_loopback);
  void HandleInput(int fd);
  bool AuthenticateInput(int fd);
  bool ValidateApiKey(std::string_view api_key) const;

  bool TrySetInput(int fd);
  bool TrySetOutput(int fd);
  void RemoveInput(int fd);
  void RemoveOutput(int fd);
  void NotifyInput(std::string_view type);

  void SetText(std::string text);
  std::string GetText() const;
  void ClearText();
  void ScheduleDebounce();
  void CancelDebounce();
  void ForceDebounce();
  void DebounceLoop();
  void SendEmptyCommit(int fd);
  void SendFinalResult();

  Settings settings_;
  std::mutex lifecycle_mutex_;
  std::atomic<bool> running_{false};
  bool service_started_ = false;
  int listen_fd_ = -1;
  std::thread accept_thread_;
  std::thread debounce_thread_;

  mutable std::mutex state_mutex_;
  int input_fd_ = -1;
  int output_fd_ = -1;
  std::mutex input_write_mutex_;
  std::mutex output_write_mutex_;

  mutable std::mutex text_mutex_;
  std::string current_text_;

  std::mutex debounce_mutex_;
  std::condition_variable debounce_cv_;
  bool debounce_pending_ = false;
  bool debounce_stop_ = false;
  std::chrono::steady_clock::time_point debounce_deadline_{};

  std::mutex client_threads_mutex_;
  std::vector<std::thread> client_threads_;
  std::mutex client_fds_mutex_;
  std::vector<int> client_fds_;
};

}  // namespace vinput::daemon::remote
