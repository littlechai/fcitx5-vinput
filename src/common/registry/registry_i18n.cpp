#include "common/registry/registry_i18n.h"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

#include "common/config/core_config.h"
#include "common/utils/downloader.h"
#include "common/utils/path_utils.h"
#include "common/registry/registry_cache.h"

namespace vinput::registry {

namespace {

using json = nlohmann::json;

std::string NormalizeLocale(std::string locale) {
  if (locale.empty()) {
    return {};
  }
  const auto colon_pos = locale.find(':');
  if (colon_pos != std::string::npos) {
    locale = locale.substr(0, colon_pos);
  }
  const auto dot_pos = locale.find('.');
  if (dot_pos != std::string::npos) {
    locale = locale.substr(0, dot_pos);
  }
  const auto at_pos = locale.find('@');
  if (at_pos != std::string::npos) {
    locale = locale.substr(0, at_pos);
  }
  for (char &ch : locale) {
    if (ch == '-') {
      ch = '_';
    }
  }
  if (locale == "C" || locale == "POSIX") {
    return {};
  }
  const auto sep = locale.find('_');
  if (sep == std::string::npos) {
    if (locale == "zh") {
      return "zh_CN";
    }
    if (locale == "en") {
      return "en_US";
    }
  }
  return locale;
}

I18nMap ParseI18nJson(const std::string &content, std::string *error) {
  I18nMap map;

  try {
    json j = json::parse(content);
    if (!j.is_object()) {
      if (error) {
        *error = "i18n JSON is not an object";
      }
      return map;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
      if (it.value().is_string()) {
        map[it.key()] = it.value().get<std::string>();
      }
    }
  } catch (const std::exception &ex) {
    if (error) {
      *error = std::string("failed to parse i18n JSON: ") + ex.what();
    }
    return {};
  }

  return map;
}

I18nMap LoadLocalI18nOverrides() {
  const auto local_path =
      vinput::path::VinputConfigDir() / "i18n.local.json";
  std::ifstream ifs(local_path);
  if (!ifs) {
    return {};
  }

  std::string content(std::istreambuf_iterator<char>(ifs), {});
  std::string parse_error;
  return ParseI18nJson(content, &parse_error);
}

} // namespace

std::string DetectPreferredLocale() {
  const char *vars[] = {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"};
  for (const char *name : vars) {
    const char *value = std::getenv(name);
    if (value && *value) {
      const std::string locale = NormalizeLocale(value);
      if (!locale.empty()) {
        return locale;
      }
    }
  }
  return "en_US";
}

I18nMap FetchI18nMap(const std::string &locale,
                     const std::vector<std::string> &urls, std::string *error) {
  if (urls.empty()) {
    if (error) {
      *error = "no i18n URLs configured";
    }
    return {};
  }
  std::string content;
  vinput::download::Result result;
  vinput::download::Options options;
  options.timeout_seconds = 20;
  options.max_bytes = 1024 * 1024;
  if (!vinput::registry::cache::FetchText(
          urls, vinput::registry::cache::I18nPath(locale), options, &content,
          &result, error)) {
    return {};
  }
  return ParseI18nJson(content, error);
}

I18nMap FetchMergedI18nMap(const CoreConfig &config,
                           const std::string &preferred_locale,
                           std::string *error) {
  I18nMap merged;
  std::string fetch_error;

  const auto primary_urls = ResolveRegistryI18nUrls(config, preferred_locale);
  if (!primary_urls.empty()) {
    merged = FetchI18nMap(preferred_locale, primary_urls, &fetch_error);
  }

  if (merged.empty() && preferred_locale != "en_US") {
    const auto fallback_urls = ResolveRegistryI18nUrls(config, "en_US");
    auto fallback = FetchI18nMap("en_US", fallback_urls, &fetch_error);
    merged.insert(fallback.begin(), fallback.end());
  } else if (preferred_locale != "en_US") {
    const auto fallback_urls = ResolveRegistryI18nUrls(config, "en_US");
    auto fallback = FetchI18nMap("en_US", fallback_urls, nullptr);
    for (auto &[key, value] : fallback) {
      merged.emplace(std::move(key), std::move(value));
    }
  }

  if (error) {
    *error = fetch_error;
  }

  auto local = LoadLocalI18nOverrides();
  for (auto &[key, value] : local) {
    merged[key] = std::move(value);
  }

  return merged;
}

std::string LookupI18n(const I18nMap &map, const std::string &key,
                       const std::string &fallback) {
  const auto it = map.find(key);
  if (it == map.end() || it->second.empty()) {
    return fallback;
  }
  return it->second;
}

} // namespace vinput::registry
