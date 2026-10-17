pragma Singleton
import QtQuick

// Shared QML theme tokens. Pages read only these names;
// never hard-code a radius, font size, or shadow again.
//
// Skins follow the page mapping owned by Main.qml: the
// curtain home and the poster wall run Aurora light, every other route — the
// 报刊 home, search, settings and the player page — runs Mint light. Main.qml
// owns `skin`; nothing else may write it.
//
// Fonts: Inter, LXGW WenKai Lite, Libre Caslon Display, EB Garamond and Noto
// Serif SC are bundled fonts; the Qt application loads TTF files
// from <app>/fonts (assets/fonts in source). CJK glyphs missing from the Latin
// faces fall back through the substitutions registered in UiAssets.cpp.
QtObject {
    id: theme

    // ── Presentation ─────────────────────────────────────────────
    // `curtain` is the presentation (chrome hidden); `skin` is route-owned.
    property bool curtain: false
    property string skin: "mint"
    property bool dark: false
    property string customAccent: ""
    property bool compactList: false
    property string lyricFontSetting: "skin"
    property string lyricFocus: "readable"
    readonly property bool mint: skin !== "aurora"
    readonly property bool aurora: !mint
    // The curtain home paints the --stage-home-* ladder over the Aurora skin.
    readonly property bool stageHome: aurora && curtain

    // Device pixel ratio of the window (Main.qml binds it). Chromium draws a
    // 1px CSS border as floor(dpr) device pixels; `hairline` reproduces that
    // so rules never straddle two device rows at 125%.
    property real dpr: 1
    readonly property real hairline: Math.max(1, Math.floor(dpr)) / dpr
    function snap(v) { return Math.round(v * dpr) / dpr }
    // Rounds a box size up to whole device pixels (never clips its text).
    function snapUp(v) { return Math.ceil(v * dpr - 0.001) / dpr }
    // Blink's baseline inside a line box: ascent and descent each rounded to
    // device pixels, the half-leading floored (LayoutNG's CalculateLeadingSpace).
    // `lineBaseline` takes the font's ascent/descent in pixels (FontMetrics);
    // `cssBaseline` takes em factors and defaults to Inter's.
    function lineBaseline(ascent, descent, lineHeight) {
        var a = Math.round(ascent * dpr)
        var d = Math.round(descent * dpr)
        return (a + Math.floor((lineHeight * dpr - a - d) / 2)) / dpr
    }
    function cssBaseline(size, lineHeight, ascent, descent) {
        return lineBaseline((ascent === undefined ? 0.969 : ascent) * size,
                            (descent === undefined ? 0.241 : descent) * size, lineHeight)
    }

    // The base font unit is 14 logical pixels.
    function rem(n) { return n * 14 }
    function clamp(lo, value, hi) { return Math.min(hi, Math.max(lo, value)) }
    // A fully transparent version of `c`, so ColorAnimations fade a hover
    // fill in and out instead of passing through transparent black.
    function clear(c) { return Qt.rgba(c.r, c.g, c.b, 0) }
    // Match appearance_lyric_scale without changing the immersive size ladder.
    function pageLyricScale(size) {
        if (size <= 26) return 0.85
        if (size <= 32) return 0.85 + (size - 26) * 0.15 / 6
        if (size <= 38) return 1 + (size - 32) * 0.2 / 6
        return 1.2 + (size - 38) * 0.25 / 8
    }

    // ── Radius ladder (aurora 6/10/14/20, card 18 · mint 2/3/4/6, card 2) ──
    readonly property real rXs: mint ? 2 : 6
    readonly property real rSm: mint ? 3 : 10
    readonly property real rMd: mint ? 4 : 14
    readonly property real rLg: mint ? 6 : 20
    readonly property real rCard: mint ? 2 : 18
    readonly property real rFull: 9999

    // ── Spacing ladder ──
    readonly property real s1: 4
    readonly property real s2: 8
    readonly property real s3: 12
    readonly property real s4: 16
    readonly property real s5: 20
    readonly property real s6: 24
    readonly property real s8: 32
    readonly property real s10: 40

    // ── Type ladder ──
    readonly property int fsXs: 12
    readonly property int fsSm: 13
    readonly property int fsMd: 14
    readonly property int fsLg: 16
    readonly property int fsXl: 20
    readonly property int fs2xl: 28
    readonly property int fs3xl: 36
    readonly property real playerLyricSize: 20
    readonly property real playerTitleSize: 20
    readonly property real playerTitleCompactSize: 18
    readonly property int fwRegular: 400
    readonly property int fwMedium: 500
    readonly property int fwSemiBold: 600
    readonly property int fwBold: 700
    readonly property int fwDisplay: mint ? 400 : 600
    // --display-tracking as an em factor (multiply by the font size).
    readonly property real displayTracking: mint ? -0.01 : -0.02
    readonly property int wordmarkSize: mint ? fsSm : fsXs
    readonly property int wordmarkWeight: mint ? fwRegular : fwMedium
    readonly property real wordmarkTracking: (mint ? -0.01 : 0.06) * wordmarkSize
    // --meta-*: Mint sets small meta in italic serif, Aurora in tracked caps.
    readonly property bool metaItalic: mint
    readonly property int metaCase: mint ? Font.MixedCase : Font.AllUppercase
    readonly property real metaTracking: (mint ? 0.01 : 0.08) * fsXs
    readonly property real lhTight: 1.25
    readonly property real lhNormal: 1.5

    // ── Controls & chrome metrics ──
    readonly property real controlSm: 28
    readonly property real controlMd: 34
    readonly property real controlLg: 40
    readonly property real sidebarW: appearanceState.sidebarWidth
    readonly property real titlebarH: 36
    readonly property real topbarH: s10 + s4
    readonly property real playerbarH: mint ? 76 : 84
    readonly property real coverInBar: playerbarH - s6
    readonly property real scrollBarW: 4
    // --icon-size / --icon-stroke: every chrome icon is a 16px Lucide glyph at
    // stroke 1.75 (the CSS width/stroke override beats per-icon size props).
    readonly property real iconSize: 16
    readonly property real iconStroke: 1.75
    readonly property real disabledOpacity: 0.56
    readonly property real pressedOpacity: 0.86

    // ── Font roles ──
    readonly property string fontUi: "Inter"
    readonly property string fontDisplay: mint ? "Libre Caslon Display" : fontUi
    readonly property string fontTitle: mint ? "EB Garamond" : fontUi
    readonly property string fontMeta: mint ? "EB Garamond" : fontUi
    readonly property string fontSerif: "Noto Serif SC"
    readonly property string fontLyric: lyricFontSetting === "kai" ? "LXGW WenKai Lite" : lyricFontSetting === "song" ? "Noto Serif SC" : lyricFontSetting === "hei" ? fontUi : mint ? "EB Garamond" : "LXGW WenKai Lite"
    // UiAssets.cpp turns on typo line metrics app-wide so CJK fallbacks stop
    // inflating line boxes. WenKai lacks USE_TYPO_METRICS, so as a primary
    // face it keeps the DirectWrite (win) metrics that Chromium uses.
    readonly property bool lyricTypoMetrics: fontLyric !== "LXGW WenKai Lite"
    // --font-mono resolves to Cascadia Code on Windows 11 (UiAssets.cpp
    // substitutes Consolas where it is missing).
    readonly property string fontMono: "Cascadia Code"
    // Legacy aliases kept for older call sites.
    readonly property string sans: fontUi
    readonly property string serif: fontSerif

    // ── Motion (shared duration and easing values) ──
    readonly property int durFast: 120
    readonly property int durBase: 180
    // cubic-bezier(.2, 0, 0, 1) for Easing.BezierSpline.
    readonly property var easeStandard: [0.2, 0, 0, 1, 1, 1]
    // pageEnter: Mint .24s power3.out from 8px, Aurora .28s power2.out from 12px.
    readonly property int pageEnterMs: mint ? 240 : 280
    readonly property int pageEnterEasing: mint ? Easing.OutCubic : Easing.OutQuad
    readonly property real pageEnterY: mint ? 8 : 12

    // ── Palette ──
    // Mint light: mint-tinted newsprint. Aurora light: cool glass; the curtain
    // home swaps in the --stage-home-* paper and text ladder.
    readonly property color bg: dark ? mint ? "#101614" : "#0B0D14" : mint ? "#F4F7F2" : stageHome ? "#E6ECF3" : "#F2F4FA"
    readonly property color bgElevated: dark ? mint ? "#151D1A" : "#111520" : mint ? "#FBFCF9" : "#FFFFFF"
    readonly property color surface: dark ? mint ? "#151D1A" : Qt.rgba(1, 1, 1, .06) : mint ? "#FBFCF9" : Qt.rgba(1, 1, 1, 0.72)
    readonly property color surfaceHover: dark ? mint ? "#1C2723" : Qt.rgba(1, 1, 1, .09) : mint ? "#EAF1EB" : Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.06)
    readonly property color surfaceActive: dark ? mint ? "#23332C" : Qt.rgba(1, 1, 1, .13) : mint ? "#DDEBE2" : Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.10)
    readonly property color border: dark ? mint ? "#26342E" : Qt.rgba(1, 1, 1, .09) : mint ? "#D7E0D8" : Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.08)
    readonly property color borderStrong: dark ? mint ? "#3B5046" : Qt.rgba(1, 1, 1, .17) : mint ? "#A9B8AE" : Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.18)
    // --rule-ink: Mint's printer's rules; Aurora draws no rules (--rule-w 0).
    readonly property color ruleInk: mint ? dark ? "#CFE0D6" : "#1D2A24" : border
    readonly property bool rules: mint
    readonly property color text: dark ? mint ? "#E9F2EC" : "#F2F5FA" : mint ? "#16211C" : stageHome ? "#25354B" : "#121726"
    readonly property color text2: dark ? mint ? "#AABBB2" : "#A7B0BC" : mint ? "#4B5952" : stageHome ? "#52657B" : "#4C5670"
    readonly property color text3: dark ? mint ? "#7A8C83" : "#737F8E" : mint ? "#818D86" : stageHome ? "#73869C" : "#8089A0"
    readonly property color accent: customAccent.length > 0 ? customAccent : dark ? mint ? "#3CC495" : "#7CE0B8" : mint ? "#1F9D74" : "#2FAE84"
    readonly property color accentHover: customAccent.length ? Qt.darker(accent, 1.12) : dark ? mint ? "#5FD4AA" : "#9AEACB" : mint ? "#187F5D" : "#26946F"
    readonly property color accentContrast: dark ? "#0F1614" : "#FFFFFF"
    readonly property color accentSoft: customAccent.length ? Qt.rgba(accent.r, accent.g, accent.b, .14) : dark ? mint ? "#163A2D" : Qt.rgba(accent.r, accent.g, accent.b, .14) : mint ? "#DDF0E6" : Qt.rgba(47 / 255, 174 / 255, 132 / 255, 0.12)
    readonly property color danger: dark ? mint ? "#F17A73" : "#FF7E86" : mint ? "#B9403B" : "#D2455A"
    readonly property color warning: dark ? mint ? "#E6B15D" : "#E8B469" : "#A96B16"
    readonly property color focus: accent
    // --pane-bg (sidebar column) and --bar-bg (player bar) over --bg.
    readonly property color paneBg: mint ? "transparent" : dark ? Qt.rgba(0, 0, 0, .2) : Qt.rgba(1, 1, 1, 0.35)
    readonly property color barBg: mint ? bg : dark ? Qt.rgba(0, 0, 0, .35) : Qt.rgba(1, 1, 1, 0.55)
    readonly property color progressTrack: borderStrong
    // --play-bg / --play-fg: the round play buttons. Mint inks them.
    readonly property color playBg: mint ? ruleInk : accent
    readonly property color playFg: mint ? bg : accentContrast
    // --stage-bg / --stage-text: hover veils over artwork.
    readonly property color stageBg: mint ? "#EEF3EC" : "#05060B"
    readonly property color stageText: mint ? "#16211C" : "#F4F6FB"
    // --hero-grad / --hero-glow-opacity: Aurora's greeting ink and cover glow.
    readonly property bool heroGradient: aurora
    readonly property real heroGlowOpacity: mint ? 0 : 0.5

    // Curtain stage furniture (aurora light --stage-home-*).
    readonly property color stageCard: stageHome && !dark ? "#F7FAFE" : bgElevated
    readonly property color stageCardBorder: stageHome && !dark ? Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.10) : border
    readonly property real stageArtOpacity: stageHome ? (dark ? 0.15 : 0.31) : mint ? 0.16 : 0.62

    // ── Shadows: shared theme shadows. blurMax carries the CSS blur radius
    // (MultiEffect runs at shadowBlur 1.0), colors carry the CSS alpha. Use
    // components/ShadowEffect.qml rather than reading these directly. Mint
    // has no --shadow-1 and no --fx-glow.
    function hasShadow(level) { return !(mint && (level === 1 || level === 4)) }
    readonly property color shadow1Color: Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.06)
    readonly property int shadow1BlurMax: 2
    readonly property real shadow1Y: 1
    readonly property color shadow2Color: mint ? Qt.rgba(20 / 255, 40 / 255, 30 / 255, 0.08) : Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.10)
    readonly property int shadow2BlurMax: mint ? 24 : 30
    readonly property real shadow2Y: mint ? 8 : 10
    readonly property color shadowPopColor: mint ? Qt.rgba(20 / 255, 40 / 255, 30 / 255, 0.12) : Qt.rgba(20 / 255, 30 / 255, 60 / 255, 0.16)
    readonly property int shadowPopBlurMax: mint ? 40 : 50
    readonly property real shadowPopY: mint ? 16 : 20
    // --fx-glow on the primary play button.
    readonly property color glowColor: Qt.rgba(47 / 255, 174 / 255, 132 / 255, 0.20)
    readonly property int glowBlurMax: 48
    readonly property real glowY: 18

    // ── Poster wall (skin-independent artwork scene) ──
    readonly property color wallBg: "#101721"
    readonly property color wallSurface: "#1C2735"
    readonly property color wallInk: "#FFFFFF"
    readonly property color wallInkSecondary: Qt.rgba(1, 1, 1, 0.72)
    readonly property color wallInkMuted: Qt.rgba(1, 1, 1, 0.65)
    readonly property color wallInkDim: Qt.rgba(1, 1, 1, 0.58)
    readonly property color wallMuted: "#A4B3C5"
    readonly property color wallBorder: Qt.rgba(1, 1, 1, 0.14)
    readonly property color wallBorderStrong: Qt.rgba(1, 1, 1, 0.20)
    readonly property color wallCurrentBorder: Qt.rgba(1, 1, 1, 0.85)
    readonly property color wallCurrentBg: "#344F59"
    readonly property color wallGlassBg: Qt.rgba(13 / 255, 22 / 255, 32 / 255, 0.76)
    readonly property color wallBadgeBg: Qt.rgba(4 / 255, 12 / 255, 22 / 255, 0.52)
    readonly property color wallHover: Qt.rgba(38 / 255, 52 / 255, 68 / 255, 0.92)
    readonly property color wallSoftBg: Qt.rgba(1, 1, 1, 0.08)
    readonly property color wallPosterHover: Qt.rgba(1, 1, 1, 0.05)
    readonly property color wallControlHover: Qt.rgba(1, 1, 1, 0.15)
    // text-shadow / box-shadow pairs: color + CSS blur radius + y offset.
    readonly property color wallCaptionShadow: Qt.rgba(0, 0, 0, 0.45)
    readonly property int wallCaptionBlur: 8
    readonly property color wallGlassShadow: Qt.rgba(0, 0, 0, 0.15)
    readonly property int wallGlassBlur: 22
    readonly property color wallHeaderShadow: Qt.rgba(0, 0, 0, 1)
    readonly property color wallLyricShadow: Qt.rgba(0, 0, 0, 0.5)
    readonly property real wallControlWidth: 36
    readonly property real wallTitleClearance: 116
    readonly property color radioAccent: "#B64F69"
    // --radio-soft: color-mix(--radio-accent 12%, --bg).
    readonly property color radioSoft: dark ? Qt.rgba(radioAccent.r, radioAccent.g, radioAccent.b, 0.18) : mint ? "#EDE3E2" : "#EBE0E9"

    // Wall furniture is independent of the newspaper skin and light preference.
    readonly property int wallBarH: 44
    readonly property color wallChromeSurface: "#1C2735"
    readonly property color wallChromeInk: "#EEF2F7"
    readonly property color wallChromeMuted: Qt.rgba(wallChromeInk.r, wallChromeInk.g, wallChromeInk.b, 0.64)
    readonly property color wallChromeHover: Qt.rgba(1,1,1,0.08)
    readonly property color wallChromePressed: Qt.rgba(1,1,1,0.14)
    readonly property color wallDanger: "#D2455A"
    readonly property color wallAccent: customAccent.length ? customAccent : "#7CE0B8"
    readonly property color wallFocus: wallFocusFor(wallAccent)
    function luminance(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b)
    }
    function contrast(a, b) {
        var composite = Qt.rgba(a.r*a.a+b.r*(1-a.a), a.g*a.a+b.g*(1-a.a), a.b*a.a+b.b*(1-a.a), 1)
        var x = luminance(composite), y = luminance(b)
        return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05)
    }
    function inkOn(c) { return contrast(Qt.rgba(0,0,0,1),c) >= contrast(Qt.rgba(1,1,1,1),c) ? "#000000" : "#FFFFFF" }
    function wallFocusFor(c) { return contrast(c,wallBg) >= 3 ? c : wallChromeInk }
    function wallHoverOn(c) { return Qt.tint(c, luminance(c)>0.4 ? Qt.rgba(0,0,0,0.06) : wallChromeHover) }
    function wallDownOn(c) { return Qt.tint(c, luminance(c)>0.4 ? Qt.rgba(0,0,0,0.12) : wallChromePressed) }
    readonly property var mediaStyle: ({
        play: {glyph:"media-play",dx:1,stroke:1.5,stroked:true},
        pause: {glyph:"media-pause",dx:0,stroke:1.5,stroked:false},
        prev: {glyph:"media-prev",dx:-0.5,stroke:2,stroked:true},
        next: {glyph:"media-next",dx:0.5,stroke:2,stroked:true}
    })
    function glyphFit(glyph, box, ratio) {
        var r = ratio > 0 ? ratio : dpr
        var g = Math.round(glyph*r), b = Math.round(box*r)
        if ((b-g)%2) g--
        return Math.max(0,g)/r
    }
    function mediaOffset(kind, glyph) { return snap((mediaStyle[kind] || mediaStyle.play).dx * glyph / 24) }
    function bravaisZoom(w,h) { return Math.max(0.45,Math.min(w<640?0.52:w<1100?0.64:0.76,Math.max(0,h-wallBarH-48)/808)) }
    function worldPx(screen, zoom) { return Math.ceil(Math.max(0,screen)/Math.max(0.01,zoom)-0.001) }
    function tileTitlePx(w,zoom) { return worldPx(Math.max(15,clamp(22,0.02*w,36)*zoom),zoom)*zoom }
    function tileSubtitlePx(w,zoom) { return worldPx(clamp(12,0.52*tileTitlePx(w,zoom),14),zoom)*zoom }
    function tileInsetPx(zoom) { return Math.max(12,18*zoom) }
    function focusTitlePx(w,zoom) { return Math.round(clamp(48,0.06*w,82))*zoom }
    function focusInfoPx(zoom) { return worldPx(Math.max(13,17*zoom),zoom)*zoom }
    function coverLyricGap(s) { return clamp(64,Math.round(0.21*s),112) }
    function lyricGapInset(s,gridGap) { return Math.max(0,coverLyricGap(s)-10-gridGap-12) }
    function lyricFreeInset(w) { return clamp(24,0.08*w,64) }
    function immersiveLayout(w,h,titleLineH,artistLineH) {
        var stageTop=56, stageH=Math.max(0,h-132)
        var s=clamp(200,Math.min(0.28*w,0.5*stageH),520), g=clamp(56,0.28*s,112)
        var m=clamp(32,0.08*w,160), l=clamp(320,w-2*m-s-g,640)
        var groupX=Math.max(m,(w-s-g-l)/2)
        var artTop=stageTop+Math.max(0,(stageH-s-30-titleLineH-9-artistLineH)/2)
        return {s:s,g:g,groupX:groupX,lyricX:groupX+s+g,lyricW:l,artTop:artTop,stageTop:stageTop,stageH:stageH,focusY:artTop+s/2-stageTop}
    }

    // PlayerProgress formats mm:ss with both fields zero-padded.
    function time(millis) {
        var s = Math.max(0, Math.floor(millis / 1000))
        var m = Math.floor(s / 60)
        return (m < 10 ? "0" : "") + m + ":" + (s % 60 < 10 ? "0" : "") + s % 60
    }
    // homeFormat.formatDuration: m:ss with an unpadded minute, "0:00" when empty.
    function duration(seconds) {
        var s = Math.floor(Number(seconds))
        if (!(s > 0)) return "0:00"
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + s % 60
    }
    // CSS sizes that land between integer pixels (rem, vw clamps) need a point
    // size; Qt Quick maps 1pt to 4/3 logical px.
    function pt(px) { return px * 0.75 }
    function artwork(url, pixels) {
        return (url || "").replace(/\/stdmusic\/\d+\//, "/stdmusic/" + pixels + "/")
    }
    // Display and greeting font sizes as viewport clamps.
    function displayLg(vw) { return mint ? clamp(rem(1.9), vw * 0.029, rem(2.75)) : clamp(rem(1.75), vw * 0.026, rem(2.5)) }
    function displayXl(vw) { return mint ? clamp(rem(2.5), vw * 0.04, rem(4)) : clamp(rem(2.25), vw * 0.036, rem(3.5)) }
    function greetSize(vw) { return mint ? displayXl(vw) : clamp(rem(1.375), vw * 0.018, rem(1.75)) }
}
