#ifndef M4_NATIVE_LOG_HPP_
#define M4_NATIVE_LOG_HPP_

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

inline std::string native_log_path(const char* filename) {
  const char* configured = std::getenv("M4_NATIVE_LOG_DIR");
  const std::filesystem::path directory = configured ? configured : "/output";
  if ((directory != "/output" && directory.parent_path() != "/output") ||
      !std::filesystem::is_directory(directory)) {
    throw std::runtime_error("native log directory outside experiment output");
  }
  return (directory / filename).string();
}
#endif
