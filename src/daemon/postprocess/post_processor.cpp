#include "post_processor.h"

#include "common/llm/defaults.h"
#include "common/utils/debug_log.h"
#include "common/utils/path_utils.h"
#include "common/utils/url_utils.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <optional>

#include <string>
#include <string_view>

namespace {

using json = nlohmann::json;
constexpr std::size_t kMaxLoggedResponseBytes = 2048;
constexpr std::size_t kMaxResponseBytes = 1 * 1024 * 1024; // 1 MB limit
constexpr long kDefaultConnectTimeoutMs = 5000;

struct CurlGuard {
  CURL *curl = nullptr;
  struct curl_slist *headers = nullptr;

  ~CurlGuard() {
    if (headers) curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
  }
};

struct CurlRequestContext {
  const std::atomic<bool> *cancel_flag = nullptr;
};

size_t WriteResponseCallback(char *ptr, size_t size, size_t nmemb,
                             void *userdata) {
  const size_t total = size * nmemb;
  if (!userdata || !ptr || total == 0) {
    return 0;
  }

  auto *response = static_cast<std::string *>(userdata);
  if (response->size() + total > kMaxResponseBytes)
    return 0;
  response->append(ptr, total);
  return total;
}

int ProgressCallback(void *clientp, curl_off_t, curl_off_t, curl_off_t,
                     curl_off_t) {
  const auto *ctx = static_cast<const CurlRequestContext *>(clientp);
  if (!ctx || !ctx->cancel_flag) {
    return 0;
  }
  return ctx->cancel_flag->load(std::memory_order_relaxed) ? 1 : 0;
}

std::string TrimAsciiWhitespace(std::string text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }

  const auto end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

json BuildCandidatesResponseFormat() {
  return {
      {"type", "json_object"},
  };
}

std::string BuildRequestUrl(const std::string &base_url) {
  if (base_url.empty()) {
    return {};
  }

  constexpr std::string_view kChatCompletions =
      vinput::llm::kOpenAiChatCompletionsPath;
  if (base_url.size() >= kChatCompletions.size() &&
      base_url.compare(base_url.size() - kChatCompletions.size(),
                       kChatCompletions.size(), kChatCompletions) == 0) {
    return base_url;
  }

  return vinput::url::JoinPath(base_url, kChatCompletions);
}

std::string QuoteForLog(std::string_view text) {
  if (text.size() > kMaxLoggedResponseBytes) {
    std::string truncated(text.substr(0, kMaxLoggedResponseBytes));
    truncated += "...(truncated)";
    return truncated;
  }
  return std::string(text);
}

void LogResponseSummary(const LlmProvider &provider, const std::string &url,
                        long status_code, double total_time_ms) {
  vinput::debug::Log(
      "LLM request provider=%s url=%s status=%ld time=%.1fms\n",
      provider.id.empty() ? "(unnamed)" : provider.id.c_str(), url.c_str(),
      status_code, total_time_ms);
}

void LogLlmInput(const LlmProvider &provider, const std::string &url,
                 std::string_view text) {
  vinput::debug::Log("LLM input provider=%s url=%s: %s\n",
                     provider.id.empty() ? "(unnamed)" : provider.id.c_str(),
                     url.c_str(), QuoteForLog(text).c_str());
}

void LogResponseBody(const char *prefix, const std::string &url,
                     std::string_view body) {
  vinput::debug::Log("%s %s: %s\n", prefix, url.c_str(),
                     QuoteForLog(body).c_str());
}

std::vector<std::string> ExtractCandidates(const json &response) {
  const auto choices_it = response.find("choices");
  if (choices_it == response.end() || !choices_it->is_array() ||
      choices_it->empty()) {
    return {};
  }

  const auto &choice = (*choices_it)[0];
  const auto message_it = choice.find("message");
  if (message_it == choice.end() || !message_it->is_object()) {
    return {};
  }

  const auto content_it = message_it->find("content");
  if (content_it == message_it->end() || !content_it->is_string()) {
    return {};
  }

  json content_json;
  try {
    content_json = json::parse(content_it->get<std::string>());
  } catch (const std::exception &) {
    return {};
  }

  const auto candidates_it = content_json.find("candidates");
  if (candidates_it == content_json.end() || !candidates_it->is_array()) {
    return {};
  }

  std::vector<std::string> candidates;
  for (const auto &value : *candidates_it) {
    if (!value.is_string()) {
      continue;
    }
    auto candidate = TrimAsciiWhitespace(value.get<std::string>());
    if (!candidate.empty()) {
      candidates.push_back(std::move(candidate));
    }
  }

  return candidates;
}

void AppendMarkdownCodeBlock(std::string *out, std::string_view text) {
  if (!out) {
    return;
  }
  out->append("```text\n");
  out->append(text);
  if (text.empty() || text.back() != '\n') {
    out->push_back('\n');
  }
  out->append("```\n");
}

void AppendMarkdownTextSection(std::string *out, std::string_view heading,
                               std::string_view body) {
  if (!out) {
    return;
  }
  if (!out->empty() && out->back() != '\n') {
    out->push_back('\n');
  }
  if (!out->empty()) {
    out->push_back('\n');
  }
  out->append("## ");
  out->append(heading);
  out->append("\n\n");
  AppendMarkdownCodeBlock(out, body);
}

std::string BuildUserInputMarkdown(std::string_view source_text) {
  std::string markdown = "# Input\n";
  AppendMarkdownTextSection(&markdown, "Source Text", source_text);
  return markdown;
}

std::string BuildContextPrefix(int max_lines) {
  if (max_lines <= 0) {
    return {};
  }
  const auto path = vinput::path::ContextCachePath();
  std::ifstream ifs(path);
  if (!ifs) {
    return {};
  }
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(ifs, line)) {
    if (!line.empty()) {
      lines.push_back(std::move(line));
    }
  }
  if (lines.empty()) {
    return {};
  }
  const auto start =
      (static_cast<int>(lines.size()) > max_lines)
          ? lines.end() - max_lines
          : lines.begin();
  std::string result = "Recent input history (use to fix ASR errors):\n";
  for (auto it = start; it != lines.end(); ++it) {
    result += *it;
    result += '\n';
  }
  result += '\n';
  return result;
}

std::string BuildConstraintsSuffix(int candidate_count) {
  if (candidate_count <= 0) {
    return {};
  }
  char buf[768];
  std::snprintf(buf, sizeof(buf),
                "\n\n## Constraints\n"
                "- Return only the JSON object described below.\n"
                "- Each candidate must contain only the final rewritten text.\n"
                "- Do not include explanations, Markdown fences, or extra keys.\n"
                "\n\n## Format\n"
                "Return EXACTLY %d candidate(s) in a JSON object:\n"
                "```json\n"
                "{\"candidates\": [\"<string>\", \"<string>\"]}\n"
                "```",
                candidate_count);
  return buf;
}

// Merge user-supplied extra_body fields into the outgoing request. Some
// top-level fields are load-bearing for the candidates contract (messages
// schema, non-streaming response, json_object response_format) and are refused
// to prevent the merge from breaking response parsing. Everything else passes
// through so provider-specific knobs like enable_thinking / reasoning /
// thinking / top_p work without a whitelist.
void MergeExtraBody(json &request, const nlohmann::json &extra,
                    const LlmProvider &provider) {
  if (!extra.is_object() || extra.empty()) {
    return;
  }
  static constexpr std::array<std::string_view, 3> kProtected = {
      "messages", "stream", "response_format"};
  for (auto it = extra.begin(); it != extra.end(); ++it) {
    const std::string &key = it.key();
    if (std::find(kProtected.begin(), kProtected.end(), key) !=
        kProtected.end()) {
      vinput::debug::Log(
          "LLM extra_body provider=%s: ignoring protected key '%s'\n",
          provider.id.empty() ? "(unnamed)" : provider.id.c_str(),
          key.c_str());
      continue;
    }
    request[key] = it.value();
  }
}

std::optional<std::vector<std::string>>
RewriteWithOpenAiCompatible(const std::string &text,
                            const vinput::scene::Definition &scene,
                            const LlmProvider &provider, int candidate_count,
                            std::string *error_out,
                            const std::string &task_prompt = {},
                            bool use_markdown_user_input = true,
                            const std::atomic<bool> *cancel_flag = nullptr) {
  if (scene.prompt.empty() && task_prompt.empty()) {
    return std::nullopt;
  }

  CurlGuard guard;
  guard.curl = curl_easy_init();
  if (!guard.curl) {
    fprintf(stderr, "vinput-daemon: failed to initialize libcurl\n");
    return std::nullopt;
  }

  const std::string url = BuildRequestUrl(provider.base_url);
  if (url.empty()) {
    return std::nullopt;
  }

  const std::string &effective_task =
      task_prompt.empty() ? scene.prompt : task_prompt;

  const std::string system_content =
      effective_task + BuildConstraintsSuffix(candidate_count);
  const std::string user_content =
      use_markdown_user_input ? BuildUserInputMarkdown(text) : text;
  const std::string context_prefix = BuildContextPrefix(scene.context_lines);
  const std::string final_user_content =
      context_prefix.empty() ? user_content : (context_prefix + user_content);

  if (vinput::debug::Enabled()) {
    LogLlmInput(provider, url, text);
  }

  std::vector<json> messages;
  messages.push_back({{"role", "system"}, {"content", system_content}});
  messages.push_back({{"role", "user"}, {"content", final_user_content}});

  json request = {
      {"model", scene.model},
      {"stream", false},
      {"temperature", 0.2},
      {"response_format", BuildCandidatesResponseFormat()},
      {"messages", std::move(messages)},
  };
  MergeExtraBody(request, provider.extra_body, provider);
  const std::string request_body = request.dump();

  guard.headers =
      curl_slist_append(nullptr, vinput::llm::kJsonContentTypeHeader);
  if (!provider.api_key.empty()) {
    const std::string auth =
        std::string(vinput::llm::kAuthorizationHeader) + ": " +
        vinput::llm::kBearerPrefix + provider.api_key;
    guard.headers = curl_slist_append(guard.headers, auth.c_str());
  }

  curl_easy_setopt(guard.curl, CURLOPT_POST, 1L);
  curl_easy_setopt(guard.curl, CURLOPT_HTTPHEADER, guard.headers);
  curl_easy_setopt(guard.curl, CURLOPT_POSTFIELDS, request_body.c_str());
  curl_easy_setopt(guard.curl, CURLOPT_POSTFIELDSIZE,
                   static_cast<long>(request_body.size()));
  curl_easy_setopt(guard.curl, CURLOPT_WRITEFUNCTION, WriteResponseCallback);
  curl_easy_setopt(
      guard.curl, CURLOPT_CONNECTTIMEOUT_MS,
      static_cast<long>(
          std::min(scene.timeout_ms, static_cast<int>(kDefaultConnectTimeoutMs))));
  curl_easy_setopt(guard.curl, CURLOPT_TIMEOUT_MS, scene.timeout_ms);
  curl_easy_setopt(guard.curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(guard.curl, CURLOPT_USERAGENT, vinput::llm::kHttpUserAgent);

  std::string response_body;
  curl_easy_setopt(guard.curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(guard.curl, CURLOPT_WRITEDATA, &response_body);
  CurlRequestContext request_context{.cancel_flag = cancel_flag};
  curl_easy_setopt(guard.curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(guard.curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(guard.curl, CURLOPT_XFERINFODATA, &request_context);

  CURLcode curl_code = curl_easy_perform(guard.curl);
  long status_code = 0;
  double total_time_sec = 0.0;
  curl_easy_getinfo(guard.curl, CURLINFO_RESPONSE_CODE, &status_code);
  curl_easy_getinfo(guard.curl, CURLINFO_TOTAL_TIME, &total_time_sec);
  const double total_time_ms = total_time_sec * 1000.0;

  const bool cancelled =
      curl_code == CURLE_ABORTED_BY_CALLBACK && cancel_flag &&
      cancel_flag->load(std::memory_order_relaxed);

  if (cancelled) {
    vinput::debug::Log(
        "LLM request provider=%s url=%s cancelled during shutdown after %.1fms\n",
        provider.id.empty() ? "(unnamed)" : provider.id.c_str(),
        url.c_str(), total_time_ms);
    if (error_out) {
      error_out->clear();
    }
    return std::nullopt;
  }

  if (curl_code != CURLE_OK) {
    const std::string msg =
        std::string("LLM request failed: ") + curl_easy_strerror(curl_code);
    fprintf(stderr,
            "vinput-daemon: LLM request provider=%s url=%s failed after %.1fms: %s\n",
            provider.id.empty() ? "(unnamed)" : provider.id.c_str(),
            url.c_str(), total_time_ms, curl_easy_strerror(curl_code));
    if (error_out) *error_out = msg;
    return std::nullopt;
  }

  LogResponseSummary(provider, url, status_code, total_time_ms);

  if (status_code < 200 || status_code >= 300) {
    const std::string msg =
        "HTTP " + std::to_string(status_code) + ": " + response_body;
    if (vinput::debug::Enabled()) {
      LogResponseBody("LLM error response from", url, response_body);
    }
    if (error_out) *error_out = msg;
    return std::nullopt;
  }

  if (vinput::debug::Enabled()) {
    LogResponseBody("LLM raw response from", url, response_body);
  }

  json response;
  try {
    response = json::parse(response_body);
  } catch (const std::exception &e) {
    fprintf(stderr,
            "vinput-daemon: failed to parse LLM response JSON from %s: %s\n",
            url.c_str(), e.what());
    return std::nullopt;
  }

  const auto error_it = response.find("error");
  if (error_it != response.end()) {
    fprintf(stderr, "vinput-daemon: LLM response from %s contains error\n",
            url.c_str());
    if (vinput::debug::Enabled()) {
      LogResponseBody("LLM parsed error payload from", url, error_it->dump());
    }
    return std::nullopt;
  }

  auto candidates = ExtractCandidates(response);
  if (candidates.empty()) {
    fprintf(stderr,
            "vinput-daemon: LLM response from %s returned no valid "
            "candidates\n",
            url.c_str());
    return std::nullopt;
  }

  return candidates;
}

void AppendCandidate(vinput::result::Payload &payload, std::string text,
                     const char *source) {
  text = TrimAsciiWhitespace(std::move(text));
  if (text.empty()) {
    return;
  }

  payload.candidates.push_back(
      vinput::result::Candidate{.text = std::move(text), .source = source});
}

} // namespace

PostProcessor::PostProcessor() {
  const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK) {
    fprintf(stderr, "vinput-daemon: curl_global_init failed: %s\n",
            curl_easy_strerror(code));
  }
}

PostProcessor::~PostProcessor() { curl_global_cleanup(); }

void PostProcessor::Shutdown() {
  shutting_down_.store(true, std::memory_order_relaxed);
}

vinput::result::Payload
PostProcessor::Process(const std::string &raw_text,
                       const vinput::scene::Definition &scene,
                       const CoreConfig &settings,
                       std::string *error_out) const {
  std::string normalized = TrimAsciiWhitespace(raw_text);
  if (normalized.empty()) {
    return {};
  }

  const int candidate_count =
      vinput::scene::NormalizeCandidateCount(scene.candidate_count);

  vinput::result::Payload fallback;
  AppendCandidate(fallback, normalized, vinput::result::kSourceRaw);
  fallback.commitText = normalized;

  const LlmProvider *provider =
      ResolveLlmProvider(settings, scene.provider_id);
  if (!provider || candidate_count == 0 || scene.prompt.empty()) {
    return fallback;
  }

  auto rewritten =
      RewriteWithOpenAiCompatible(normalized, scene, *provider, candidate_count,
                                  error_out, {}, true, &shutting_down_);
  if (!rewritten.has_value()) {
    return fallback;
  }

  vinput::result::Payload payload;
  AppendCandidate(payload, normalized, vinput::result::kSourceRaw);
  for (auto &text : *rewritten) {
    AppendCandidate(payload, std::move(text), vinput::result::kSourceLlm);
  }

  payload.commitText = normalized;
  for (const auto &candidate : payload.candidates) {
    if (candidate.source == vinput::result::kSourceLlm) {
      payload.commitText = candidate.text;
      break;
    }
  }

  return payload;
}

vinput::result::Payload
PostProcessor::ProcessCommand(const std::string &asr_text,
                              const std::string &selected_text,
                              const vinput::scene::Definition &command_scene,
                              const CoreConfig &settings,
                              std::string *error_out) const {
  std::string normalized_asr = TrimAsciiWhitespace(asr_text);

  vinput::result::Payload fallback;
  const std::string &fallback_text =
      normalized_asr.empty() ? selected_text : normalized_asr;
  AppendCandidate(fallback, fallback_text, vinput::result::kSourceRaw);
  fallback.commitText = fallback_text;

  if (normalized_asr.empty() || selected_text.empty()) {
    return fallback;
  }

  const int command_candidate_count =
      vinput::scene::NormalizeCandidateCount(command_scene.candidate_count);

  const LlmProvider *provider =
      ResolveLlmProvider(settings, command_scene.provider_id);
  if (!provider || command_candidate_count == 0) {
    return fallback;
  }

  // task prompt: scene prompt header + raw voice command
  std::string task_prompt = command_scene.prompt;
  if (!task_prompt.empty() && task_prompt.back() != '\n') {
    task_prompt.push_back('\n');
  }
  task_prompt += normalized_asr;

  auto rewritten =
      RewriteWithOpenAiCompatible(selected_text, command_scene, *provider,
                                  command_candidate_count, error_out,
                                  task_prompt, false, &shutting_down_);

  vinput::result::Payload payload;
  // 1st: original selected text (always)
  AppendCandidate(payload, selected_text, vinput::result::kSourceRaw);
  // 2nd: ASR recognized command (always)
  AppendCandidate(payload, normalized_asr, vinput::result::kSourceAsr);
  // 3rd+: LLM results (if available)
  if (rewritten.has_value()) {
    for (auto &text : *rewritten) {
      AppendCandidate(payload, std::move(text), vinput::result::kSourceLlm);
    }
  }

  // commitText is the first LLM result
  payload.commitText = selected_text;
  for (const auto &c : payload.candidates) {
    if (c.source == vinput::result::kSourceLlm) {
      payload.commitText = c.text;
      break;
    }
  }

  return payload;
}
