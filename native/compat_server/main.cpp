#include <charconv>
#include <iostream>
#include <exception>
#include <string_view>

namespace echo::compat_server {
int RunCompatServer(const char* host, int port);
}

int main(int argc, char** argv) {
  const char* host = "127.0.0.1";
  int port = 6609;

  for (int index = 1; index < argc; ++index) {
    std::string_view arg(argv[index]);
    if (arg.rfind("--host=", 0) == 0) {
      host = argv[index] + 7;
    } else if (arg.rfind("--port=", 0) == 0) {
      const auto value = arg.substr(7);
      std::from_chars(value.data(), value.data() + value.size(), port);
    }
  }

  try {
    std::cout << "EchoCompatServer starting on " << host << ':' << port << '\n';
    return echo::compat_server::RunCompatServer(host, port);
  } catch (const std::exception& error) {
    std::cerr << "EchoCompatServer fatal error: " << error.what() << '\n';
    return 2;
  }
}
