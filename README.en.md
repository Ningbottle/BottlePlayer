# BottleMusic Qt

A Windows 10/11 x64 music player built with Qt Quick, QML and C++20. The native service boundary is `EchoCAPI`.

[中文](README.md) · [Documentation](docs/README.md) · [Contributing](CONTRIBUTING.md) · [Privacy](PRIVACY.md) · [Security](SECURITY.md)

This is a development preview. The current source tree and documentation describe the Qt application. Existing release assets are not evidence of a stable Qt release.

The application includes search, queue playback, lyrics, daily songs, album discovery, continuous recommendations, account and playlist features, listening statistics, audio device selection, an equalizer and window-contained immersive lyrics.

Use MSVC x64, CMake 3.24+, Ninja, Qt 6.8.3 and the dependencies in `native/vcpkg.json`. See [build and test](docs/development/build-and-test.md) for Qt modules, dependency installation, packaging and isolated test profiles.

```powershell
./tools/build.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
./tools/test.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
```

`desktop/qt/qml` owns views; `desktop/qt/src` owns Qt controllers and media; `native` owns services, execution and storage. Start with [current status](docs/development/status.md) before changing behavior.

See [LICENSE](LICENSE) and [third-party notices](desktop/qt/licenses/THIRD_PARTY.md). This is an unofficial client; source licensing does not grant rights to third-party services or music content.
