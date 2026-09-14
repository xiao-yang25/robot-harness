#include <string_view>

int main() {
  constexpr std::string_view profile{"TRUSTED_EMBEDDED"};
  return profile.empty() ? 1 : 0;
}
