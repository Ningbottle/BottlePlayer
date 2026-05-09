# EchoMusic Native

Windows-native migration scaffold for EchoMusic.

The first stage provides:

- `EchoCore`: backend facade, authorization parsing, compatibility API routing, and WinHTTP client plumbing.
- `EchoStorage`: SQLite-backed device/session/API cache storage.
- `EchoCompatServer`: temporary HTTP server on `127.0.0.1:6609` for the existing Electron/Vue renderer.
- `EchoPlayback`: Media Foundation playback-controller state-machine scaffold.
- `EchoWin32`: pure Win32 + Direct2D/DirectWrite shell scaffold.

Build with a Visual Studio developer shell and a vcpkg toolchain:

```powershell
cmake -S native -B native/out/build -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build native/out/build --config Debug
ctest --test-dir native/out/build -C Debug
```

`EchoCompatServer` returns a consistent native-not-implemented JSON payload for renderer routes that have not yet been ported from the Node `KuGouMusicApi` implementation.

