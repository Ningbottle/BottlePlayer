# Native Core dependency direction

This document records the dependency boundaries that already exist in the
native backend. It is an ownership guide, not a file-move plan: ADR-007 keeps
the current C++ and header paths unchanged for this remediation.

## Directory is architecture

```text
native/
├── core/
│   ├── C_API.cpp                 exported C ABI composition root
│   ├── CompatApi.cpp             route recognition and dispatch
│   ├── CompatRequestContext.cpp  request-scoped session/device access
│   ├── compat_routes/            seven route-owner groups
│   │   ├── DiagnosticsRoutes.cpp
│   │   ├── LoginRoutes.cpp
│   │   ├── MediaRoutes.cpp
│   │   ├── PlaylistRoutes.cpp
│   │   ├── RegisterRoutes.cpp
│   │   ├── UserRoutes.cpp
│   │   └── YouthVipRoutes.cpp
│   ├── *Service.cpp              fifteen protocol/domain services
│   └── protocol infrastructure   HTTP, signing, DTO and parsing helpers
├── include/echo/                 public headers mirroring native owners
├── storage/                      database and repositories
├── stats/                        play-stat persistence service
├── async/                        queues, schedulers and watchdogs
├── diagnostics/                  logging, timing, memory and redaction
├── image/                        image loading/cache; separate from EchoCore
└── tests/                        native contract and resilience tests
```

`core/` is physically flat except for `compat_routes/`. The categories below
describe ownership inside that existing directory; they do not authorize new
`core/`, `utils/`, or helper directories.

## Build-target graph

`native/CMakeLists.txt` is the source of truth for compiled dependencies:

```text
EchoCAPI (SHARED: core/C_API.cpp)
└── EchoCore (STATIC)
    ├── EchoStorage
    ├── EchoDiagnostics
    ├── EchoAsync
    └── nlohmann_json

EchoImage (STATIC)
└── EchoAsync
```

- `EchoCore` contains the compatibility router, the domain services, protocol
  infrastructure, and `stats/PlayStatsService.cpp`.
- `EchoCAPI` is the only target that compiles `core/C_API.cpp`; it links
  `EchoCore` rather than compiling core sources a second time.
- `EchoImage` is an independent native library. `EchoCore` does not link or
  include it, so it must not be presented as part of the request dispatch
  chain.
- Rust/Tauri loads the exported C ABI dynamically. Native C++ does not compile
  against Rust, Tauri, Vue, or UI headers.

## Request path

The normal request path is:

```text
Rust/Tauri dynamic loader
└── C API                         core/C_API.cpp
    ├── process lifecycle         EchoContext + RequestScheduler
    ├── direct native commands    play stats / diagnostics
    └── CompatApi                 core/CompatApi.cpp
        ├── route table
        ├── CompatRequestContext
        └── compat_routes/        seven route groups
            ├── domain services   fifteen services listed below
            ├── storage repos     session/device and route persistence
            └── infrastructure    HTTP/signing/parsing/diagnostics
```

The route table in `CompatApi.cpp` is the single source of truth for route
recognition and dispatch. `CompatRoutes.h` declares the seven route groups;
individual `compat_routes/*.cpp` files own their handler implementations.

## Owners

### C ABI and compatibility boundary

| Owner | Responsibility |
| --- | --- |
| `C_API.cpp` / `C_API.h` | Stable exported ABI, process lifecycle, async request scheduling, error containment and response allocation |
| `CompatApi` | Known-route table, HTTP-method policy, dispatch and response redaction |
| `CompatRequestContext` | Per-request lazy access to session and device repositories |
| `compat_routes/` | Translation from compatibility routes to services, repositories and protocol responses |

`C_API.cpp` is a composition root, so its direct use of `async/`, `storage/`,
`diagnostics/`, and `stats/` is intentional. Those modules must not depend back
on the C ABI.

### Domain/protocol services

The fifteen `*Service.cpp` files compiled by `EchoCore` are:

```text
CatalogService       DeviceService         DeviceRegisterService
HomeService          LoginService          LyricService
PlayHistoryService   PlaylistService       PrivilegeService
RankService          SearchService         SongService
SongUrlService       UserService           UserCloudService
```

Services own KuGou-facing domain/protocol operations. They may consume domain
DTOs, storage interfaces required by their responsibility, protocol helpers,
HTTP transport, and diagnostics. They must not know about Vue components,
Pinia stores, Tauri commands, frontend gateways, or screen-specific response
models.

### Protocol infrastructure inside `core/`

```text
HttpClient / HttpUtils
Crypto / Authorization
KuGouProfile / KuGouAndroidRequest
JsonHelpers / StringUtils / LyricParser
Dto / RequestDeadlines / SafeStoll / CompatApiUtils
```

These files support transport, signing, parsing, validation, and protocol data.
They are not new application-wide `utils`; their owner remains the native
KuGou protocol implementation.

The current flat layer has two deliberate facts that future work must account
for instead of hiding:

- route handlers access storage repositories directly for request/session
  composition;
- `KuGouAndroidRequest` consumes device information through `DeviceService`.

Changing either edge requires a separate behavior-preserving task. This README
does not claim that the physical `core/` directory is already a strict layered
package.

### Supporting native modules

| Directory | Owner and allowed consumers |
| --- | --- |
| `storage/` | Database, app paths, cache and repositories; consumed by composition, routes, device/stat services |
| `stats/` | Play-history statistics persisted through `storage::Database`; composed by the C API |
| `async/` | Task/request scheduling, event queue and watchdog; consumed by C API, HTTP and image loading |
| `diagnostics/` | Native diagnostics, timers, memory snapshots and redaction; may be consumed by upper native layers |
| `image/` | Image cache/loader in the separate `EchoImage` target; depends on `EchoAsync`, not on `EchoCore` |

## Dependency rules

Allowed dependencies point downward or inward toward a narrower native owner:

```text
C ABI composition
↓
CompatApi and route handlers
↓
domain/protocol services
↓
protocol infrastructure and supporting native modules
```

The build-target graph above takes precedence over this conceptual flow when
the two differ.

The following rules are mandatory:

1. **Native must not include UI or Tauri headers.** No file under `native/`
   may include or depend on Vue/UI sources, `ui/src-tauri`, or Tauri headers.
2. **Services must not construct UI schemas.** A service may return native
   domain DTOs or KuGou protocol JSON, but it must not construct a Vue view
   model, Pinia state shape, presentation copy, or frontend gateway contract.
3. **`server/` is reference-only.** The Git submodule may be inspected for
   manual protocol comparison, but it must not enter a native CMake target,
   production build, packaging step, or runtime dependency.

Additional direction rules:

- `storage/`, `async/`, `diagnostics/`, and `image/` must not depend on the C
  ABI or compatibility route handlers.
- `compat_routes/` owns compatibility translation; services must not dispatch
  routes themselves.
- `EchoImage` remains separate from `EchoCore` unless a future ADR changes the
  target graph with tests and an explicit owner.
- New native dependencies must be represented in `CMakeLists.txt`; incidental
  include reachability is not an architectural contract.

## Audit commands

Run from the repository root:

```powershell
# Seven compatibility route implementation groups.
(Get-ChildItem native/core/compat_routes -Filter '*Routes.cpp').Count

# Fifteen domain/protocol service implementations in core/.
(Get-ChildItem native/core -Filter '*Service.cpp').Count

# UI/Tauri headers must not be included by Native. Expected: exit 1, no output.
rg -n '#include.*(tauri|ui/|src-tauri|frontend)' native -g '*.cpp' -g '*.h'

# server/ must not participate in native build files. Expected: exit 1, no output.
rg -n 'add_subdirectory\(.*server|target_(sources|link_libraries).*server|server/' native -g 'CMakeLists.txt' -g '*.cmake'

# CMake target ownership and direction.
rg -n 'add_library|target_link_libraries|core/compat_routes|core/C_API.cpp' native/CMakeLists.txt
```

For both negative `rg` gates, exit `1` with empty output is PASS. Exit `2` is a
command error and must not be reported as a clean boundary.
