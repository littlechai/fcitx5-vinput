#include "daemon/postprocess/prompt_template.h"

#include "common/utils/debug_log.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <regex>
#include <sys/stat.h>

namespace vinput::prompt_template {

namespace {

constexpr std::string_view kFileUriPrefix = "file:///";
constexpr size_t kMaxPromptFileBytes = 256 * 1024;  // 256 KiB safety cap

}  // namespace

bool IsFileUri(std::string_view s) {
  return s.size() >= kFileUriPrefix.size() &&
         s.substr(0, kFileUriPrefix.size()) == kFileUriPrefix;
}

bool HasInterpolation(std::string_view s) {
  return s.find("{{") != std::string_view::npos;
}

std::optional<std::string> LoadFromFileUri(std::string_view uri,
                                           std::string *error) {
  auto fail = [&](const char *msg) -> std::optional<std::string> {
    if (error) {
      *error = msg;
    }
    return std::nullopt;
  };

  if (!IsFileUri(uri)) {
    return fail("not a file:/// URI");
  }
  // Strip `file://` (two slashes) but keep the leading `/` of the absolute
  // path. Result: `/abs/path`.
  const std::string_view path_view = uri.substr(kFileUriPrefix.size() - 1);
  if (path_view.empty() || path_view == "/") {
    return fail("empty path");
  }
  const std::string path(path_view);

  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    if (error) {
      *error = std::string("stat failed: ") + std::strerror(errno);
    }
    return std::nullopt;
  }
  if (!S_ISREG(st.st_mode)) {
    return fail("not a regular file");
  }

  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    return fail("open failed");
  }

  // Read at most kMaxPromptFileBytes+1 bytes; the extra byte lets us detect
  // oversize without loading the whole file.
  std::string content;
  content.resize(kMaxPromptFileBytes + 1);
  ifs.read(&content[0], static_cast<std::streamsize>(content.size()));
  const std::streamsize got = ifs.gcount();
  const size_t read_bytes = got > 0 ? static_cast<size_t>(got) : 0;
  content.resize(read_bytes);

  if (content.size() > kMaxPromptFileBytes) {
    vinput::debug::Log(
        "prompt_template: file '%s' truncated to %zu bytes (cap)\n",
        path.c_str(), kMaxPromptFileBytes);
    content.resize(kMaxPromptFileBytes);
  }
  return content;
}

std::string Interpolate(std::string_view tpl, const Vars &vars) {
  // Match `{{name}}` with optional inner whitespace; `\w+` keeps the name
  // restricted to a sane identifier set so stray braces in prose don't
  // accidentally swallow content.
  static const std::regex kVarRe(R"(\{\{\s*(\w+)\s*\}\})");

  const std::string input(tpl);
  std::string out;
  out.reserve(input.size());

  auto it = std::sregex_iterator(input.begin(), input.end(), kVarRe);
  const auto end = std::sregex_iterator();
  size_t last = 0;
  for (; it != end; ++it) {
    const auto &match = *it;
    const size_t pos = static_cast<size_t>(match.position());
    out.append(input, last, pos - last);

    const std::string name = match[1].str();
    if (name == "result") {
      out.append(vars.result);
    } else if (name == "context") {
      out.append(vars.context);
    } else {
      // Unknown placeholder — preserve verbatim so authors can write literal
      // `{{foo}}` without escaping.
      out.append(match.str());
    }
    last = pos + match.length();
  }
  out.append(input, last, std::string::npos);
  return out;
}

}  // namespace vinput::prompt_template
