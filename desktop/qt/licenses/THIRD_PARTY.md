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

The original 68 Qt interface SVG resources are unmodified official Lucide files (ISC),
at commit `6bc1373b3f3075fbcfea542ea5530fdbd6cb99b2`:
https://github.com/lucide-icons/lucide/tree/6bc1373b3f3075fbcfea542ea5530fdbd6cb99b2/icons
`Lucide-mapping.json` records canonical names, including media aliases and the
renamed history/trash icons. `Lucide-provenance.json` records the exact version;
`Lucide-LICENSE` is distributed beside the executable. The SVG paths and
viewBoxes are unchanged. Tint, media fill, stroke width and the pause glyph's
stroke suppression are applied by IconProvider at render time; SVG files are
not rewritten.
The four `transport-*.svg` controls and `lyrics.svg` are original BottleMusic
geometry, using simple triangles, bars and text lines. They are separate from
the preserved Lucide source files and provenance mapping.
The retained Phosphor notice documents the previous preview; no Phosphor SVGs
remain in the current interface.
The Inter, EB Garamond, Libre Caslon Display, Noto Serif SC and LXGW WenKai
Lite TTF files use the SIL Open Font License. Their notices are included as `Inter-LICENSE`,
`EBGaramond-LICENSE`, `LibreCaslonDisplay-LICENSE`, `NotoSerifSC-LICENSE`, and
`LXGWWenKaiLite-LICENSE`.
`NotoSerifSC-500.ttf` and `NotoSerifSC-Display.ttf` are static wght 500 and
600 instances of the OFL Noto Serif SC variable font, subset to the same
character set as `NotoSerifSC-400.ttf`; the Display file is renamed to the
family "Noto Serif SC Display" (OFL permits renaming modified versions).

BottleMusic uses MiSans for interface font fallback. The four bundled
weights identify themselves as Version 2.000 and retain their embedded
copyright notices. `MiSans-LICENSE` records the bundled files, upstream
source and agreement checksum. The complete official terms are preserved
unchanged in `MiSans-License-Agreement.pdf` and distributed beside the
application. MiSans is included as part of the application, rather than
provided as a standalone font download.

No new cover images are generated or substituted.

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

## Radix Colors

The sRGB light and dark scales in `qml/RadixColors.qml` are from `@radix-ui/colors` 3.0.0, published by WorkOS and Modulz under the MIT license. Source: https://www.radix-ui.com/colors. The full license is in `RadixColors-LICENSE`.
