// Exercise the Android-enabled SDK number parser on the physical device.
// This standalone diagnostic never loads game assets or initializes the engine.
#include <android/api-level.h>
#include <cstdio>
#include <cstring>
#include <rex/string/numeric.h>

namespace {
unsigned passed = 0;
unsigned failed = 0;

template <typename T>
void Check(const char* text, std::errc expected_error, size_t consumed, T expected) {
  T value = T(7);
  const size_t size = std::strlen(text);
  const auto result = rex::string::detail::portable_float_from_chars(text, text + size, value);
  const bool ok = result.ec == expected_error && size_t(result.ptr - text) == consumed &&
                  (expected_error == std::errc() ? value == expected : value == T(7));
  if (ok) {
    ++passed;
  } else {
    ++failed;
    std::fprintf(stderr, "Number parser case failed: %s\n", text);
  }
}
}  // namespace

int main() {
  Check<float>("1.25", {}, 4, 1.25f);
  Check<double>("-2.5e2", {}, 6, -250.0);
  Check<float>("3.5suffix", {}, 3, 3.5f);
  Check<double>("", std::errc::invalid_argument, 0, 0);
  Check<float>(" +1", std::errc::invalid_argument, 0, 0);
  Check<float>("+1", std::errc::invalid_argument, 0, 0);
  Check<double>("invalid", std::errc::invalid_argument, 0, 0);
  Check<double>("1e999", std::errc::result_out_of_range, 5, 0);
  Check<float>("1e-999", std::errc::result_out_of_range, 6, 0);
  std::printf("{\"api_level\":%d,\"number_parser_passed\":%u,\"number_parser_failed\":%u}\n",
              android_get_device_api_level(), passed, failed);
  return failed ? 1 : 0;
}
