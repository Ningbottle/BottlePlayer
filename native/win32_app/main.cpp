#include <windows.h>

namespace echo::win32_app {
int Run(HINSTANCE instance, int showCommand);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  return echo::win32_app::Run(instance, showCommand);
}

