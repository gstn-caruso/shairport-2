#include <expected>
#include <format>
#include <span>
#include <thread>

#if __cplusplus <= 202302L || !defined(__STRICT_ANSI__)
#error C++26 without extensions is required
#endif

#if !defined(__GLIBCXX__) || _GLIBCXX_RELEASE != 15
#error libstdc++ 15 is required
#endif

int main() {
  int values[] = {26};
  std::span<int> view(values);
  std::jthread worker([&] { view.front() += 1; });
  worker.join();
  std::expected<int, int> result = view.front();
  std::expected<int, int> failure = std::unexpected(1);
  return result.has_value() && !failure.has_value() &&
                 std::format("{}", *result) == "27" && failure.error() == 1
             ? 0
             : 1;
}
