# Immersive playback resources

The adapted `src/visuals/ColorExtractor.{h,cpp}` and
`shaders/immersive-fluid.frag` originate from the user's extracted QueMusic
lyric-page resource bundle (`output/que-lyric-page`, supplied 2026-10-12).
Upstream: https://github.com/BroNekoX/QueMusic

Copyright (c) 2025-2026 QueMusic Contributors. Apache-2.0.
Full license and upstream attribution are included as `QueMusic-LICENSE`
and `QueMusic-NOTICE` and copied beside the executable by CMake.

BottleMusic modifications (2026-10-12): request-generation checks before
network I/O, cancellation and cached/fallback palette handling; QML
ShaderEffect uniforms, premultiplied opacity, banding dither, and slower
animation. New playback layout, controls, and lyric-follow logic are
BottleMusic code. No QueMusic application shell, fonts, icons, API layer,
TagLib, QWindowKit, or other upstream third-party code is incorporated.

BottleMusic retains its MIT license; the adapted files retain Apache-2.0.

# Login QR encoding

`third_party/qrcodegen/qrcodegen.{hpp,cpp}` is the C++ QR Code generator by
Project Nayuki, pinned to v1.8.0, under the MIT License.
Source: https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/cpp
Documentation: https://www.nayuki.io/page/qr-code-generator-library
The original headers are preserved and `Nayuki-LICENSE` is copied beside the
executable. Login QR images are encoded locally from the native login payload.

# Fonts and icons

All 68 Qt interface SVG resources are unmodified official Lucide files (ISC),
at commit `6bc1373b3f3075fbcfea542ea5530fdbd6cb99b2`:
https://github.com/lucide-icons/lucide/tree/6bc1373b3f3075fbcfea542ea5530fdbd6cb99b2/icons
`Lucide-mapping.json` records canonical names, including media aliases and the
renamed history/trash icons. `Lucide-provenance.json` records the exact version;
`Lucide-LICENSE` is distributed beside the executable. The SVG paths and
viewBoxes are unchanged. Tint, media fill, stroke width and the pause glyph's
stroke suppression are applied by IconProvider at render time; SVG files are
not rewritten.
The retained Phosphor notice documents the previous preview; no Phosphor SVGs
remain in the current interface.
The runtime TTF files are prepared from the existing webfont packages. Their
SIL Open Font License notices are included as `Inter-LICENSE`,
`EBGaramond-LICENSE`, `LibreCaslonDisplay-LICENSE`, `NotoSerifSC-LICENSE`, and
`LXGWWenKaiLite-LICENSE`. No new cover images are generated or substituted.

Qt is dynamically linked. Its runtime remains replaceable beside the executable;
the source, build instructions and Qt 6.8.3 private-header dependency are retained.

# Bravais reference provenance

The four existing coordinate tables in `qml/components/BravaisLayout.qml`
were measured from Folia's `blockTemplates.ts` (reference revisions
`7701347` / `fb510b9`, [upstream](https://github.com/chthollyphile/folia-major),
`src/components/wall/blockTemplates.ts`). Folia is
licensed under AGPL-3.0; the upstream license is retained as `Folia-LICENSE`.
This records the table's provenance rather than attributing it to BottleMusic.
The 2026-10-16 focus reflow table and nearest-centre assignment are independently
derived in BottleMusic. No Folia React components or application runtime are
incorporated. Visual comparisons use the reference's local test harness.
