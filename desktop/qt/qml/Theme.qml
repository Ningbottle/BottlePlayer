pragma Singleton
import QtQuick

// Shared semantic tokens. Radix Colors 3.0.0 owns the palette; layout and
// controls consume roles instead of generating new colors.
QtObject {
    id: theme

    property bool dark: false
    property string customAccent: ""
    readonly property bool customized: customAccent.length > 0
    readonly property color accentSeed: customized ? customAccent : RadixColors.scale("sand", false)[8]
    property bool compactList: false
    property string lyricFontSetting: "skin"
    property string lyricFocus: "readable"
    // Text and artwork origins still snap to device pixels. Rules use the
    // requested half-logical-pixel weight, independently of screen scaling.
    property real dpr: 1
    readonly property real hairline: 0.5
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
    // Preserve stored lyric preferences while rendering on the shared ladder.
    function lyricPixels(size) { return size <= 26 ? fsXl : fs2xl }

    // Flat content panels; only controls and overlays have rounded corners.
    readonly property real rXs: 2
    readonly property real rSm: 4
    readonly property real rMd: 8
    readonly property real rLg: 12
    readonly property real rCard: 4
    readonly property real rFull: 9999
    function innerRadius(outer, inset) { return Math.max(0, outer - inset) }

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
    readonly property int fsSm: 14
    readonly property int fsMd: 14
    readonly property int fsLg: 14
    readonly property int fsXl: 20
    readonly property int fs2xl: 28
    readonly property real playerTitleSize: 20
    readonly property real playerTitleCompactSize: 14
    readonly property int fwRegular: 400
    readonly property int fwMedium: 500
    readonly property int fwSemiBold: 600
    readonly property int fwBold: 700
    readonly property int fwDisplay: 400
    // --display-tracking as an em factor (multiply by the font size).
    readonly property real displayTracking: -0.01
    readonly property int wordmarkSize: fsSm
    readonly property int wordmarkWeight: fwRegular
    readonly property real wordmarkTracking: -0.01 * wordmarkSize
    // --meta-*: Mint sets small meta in tracked caps like a newspaper eyebrow
    // (italic survives only on explicit latin accents); Aurora keeps its caps.
    readonly property bool metaItalic: false
    readonly property int metaCase: Font.MixedCase
    readonly property real metaTracking: 0.01 * fsXs
    readonly property real lhTight: 1.25
    readonly property real lhNormal: 1.5

    // ── Controls & chrome metrics ──
    readonly property real controlSm: 28
    readonly property real controlMd: 34
    readonly property real controlLg: 40
    readonly property real sidebarW: appearanceState.sidebarWidth
    readonly property real titlebarH: 36
    readonly property real topbarH: s10 + s4
    readonly property real playerbarH: 76
    readonly property real coverInBar: playerbarH - s6
    readonly property real scrollBarW: 4
    // Chrome uses an 18px outline grid. Solid transport geometry is separate
    // from the preserved Lucide source assets.
    readonly property real iconSize: 18
    readonly property real iconStroke: 1.6
    readonly property real disabledOpacity: 0.56
    readonly property real pressedOpacity: 0.86

    // ── Font roles ──
    readonly property string fontUi: "Inter"
    readonly property string fontDisplay: "Libre Caslon Display"
    readonly property string fontTitle: "EB Garamond"
    // Reading-size labels use the UI face: the display serifs' thin strokes
    // and low Latin x-height do not hold up at 12–14 logical pixels.
    readonly property string fontMeta: fontUi
    function displayFont(size) { return size <= fsMd ? fontUi : fontDisplay }
    readonly property string fontSerif: "Noto Serif SC"
    // Static wght=600 outlines in a separate family; asking the regular
    // family for 600 makes DirectWrite synthesize bold from its 500 face.
    readonly property string fontSerifDisplay: "Noto Serif SC Display"
    readonly property string fontLyric: lyricFontSetting === "kai" ? "LXGW WenKai Lite" : lyricFontSetting === "song" ? "Noto Serif SC" : lyricFontSetting === "hei" ? fontUi : "EB Garamond"
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

    // State feedback is immediate. Only spatial changes and panel reveals
    // animate, with one restrained ease-out duration.
    readonly property int durFast: 250
    readonly property int durBase: 250
    readonly property int durHover: 250
    readonly property int durTab: 250
    readonly property int durPop: 250
    readonly property int durQueueIn: 250
    readonly property int durQueueOut: 250
    readonly property var easeStandard: [0,0,0.58,1,1,1]
    readonly property var easeSoft: easeStandard
    readonly property var easeSnap: easeStandard
    readonly property var easeBounce: easeStandard
    readonly property var easeExpand: easeStandard
    readonly property real pressScale: 1
    readonly property real scrollFade: 28
    readonly property real dragDeadzone: 4
    readonly property int pageEnterMs: 0
    readonly property int pageEnterEasing: Easing.OutCubic
    readonly property real pageEnterY: 0

    readonly property string accentFamily: customized ? RadixColors.familyFor(accentSeed) : "tomato"
    readonly property string neutralFamily: customized ? RadixColors.neutralFor(accentFamily) : "sand"
    readonly property var accentScale: RadixColors.scale(accentFamily, dark)
    readonly property var neutralScale: RadixColors.scale(neutralFamily, dark)
    readonly property var wallScale: RadixColors.scale(neutralFamily, true)
    readonly property var wallAccentScale: RadixColors.scale(accentFamily, true)
    readonly property var accentChoices: [""].concat(RadixColors.accents.map(function(f) { return RadixColors.scale(f,false)[8] }))
    readonly property var accentNames: ["默认"].concat(RadixColors.names)
    function radix(family, step, night) { return RadixColors.scale(family, night === undefined ? dark : night)[step-1] }
    function alpha(c, opacity) { return Qt.rgba(c.r, c.g, c.b, opacity) }
    // Radix blackA / whiteA steps for image scrims and transient overlays.
    function blackA(step) { return Qt.rgba(0,0,0,[.05,.1,.15,.2,.3,.4,.5,.6,.7,.8,.9,.95][step-1]) }
    function whiteA(step) { return Qt.rgba(1,1,1,[.05,.1,.15,.2,.3,.4,.5,.6,.7,.8,.9,.95][step-1]) }
    readonly property color white: "#ffffff"
    readonly property color black: "#000000"
    // Steps 1–2: page/panel, 3–5: control states, 6–8: borders/focus,
    // 9–10: solid actions, 11–12: text. Keep each upstream scale intact.
    readonly property color bg: customized ? accentScale[0] : neutralScale[0]
    readonly property color bgElevated: neutralScale[1]
    readonly property color surface: neutralScale[2]
    readonly property color surfaceHover: neutralScale[3]
    readonly property color surfaceActive: neutralScale[4]
    readonly property color border: neutralScale[5]
    readonly property color borderStrong: neutralScale[6]
    readonly property color borderSoft: neutralScale[5]
    readonly property color ruleInk: neutralScale[6]
    readonly property bool rules: true
    readonly property color paperVeil: neutralScale[1]
    readonly property color text: neutralScale[11]
    readonly property color text2: contrast(neutralInk, surfaceActive) >= 4.5 ? neutralInk : text
    readonly property color neutralInk: neutralScale[10]
    readonly property color text3: text2
    readonly property color accent: accentScale[8]
    readonly property color accentHover: accentScale[9]
    readonly property color accentPressed: accentScale[9]
    readonly property color accentContrast: inkOn(accent)
    readonly property color accentSoft: accentScale[2]
    readonly property color accentText: contrast(accentInk,accentSoft) >= 4.5 ? accentInk : accentScale[11]
    readonly property color accentInk: accentScale[10]
    readonly property color seal: accentText
    readonly property color sealSoft: accentSoft
    readonly property color mark: accentText
    readonly property color danger: radix("red",11)
    readonly property color warning: radix("amber",11)
    readonly property color focus: accentScale[7]
    readonly property color paneBg: customized ? accentScale[1] : neutralScale[1]
    readonly property color barBg: paneBg
    readonly property color progressTrack: neutralScale[6]
    readonly property color playBg: accent
    readonly property color playFg: accentContrast
    readonly property color playHoverBg: accentHover
    readonly property color playPressedBg: accentPressed
    readonly property color stageBg: wallSurface
    readonly property color stageText: wallInk
    readonly property bool heroGradient: false
    readonly property real heroGlowOpacity: 0
    readonly property color stageCard: bgElevated
    readonly property color stageCardBorder: border
    readonly property real stageArtOpacity: 0.15

    // Content is flat. A small shadow is reserved for floating overlays.
    function hasShadow(level) { return level === 3 }
    readonly property color shadow1Color: blackA(1)
    readonly property int shadow1BlurMax: 0
    readonly property real shadow1Y: 0
    readonly property color shadow2Color: blackA(1)
    readonly property int shadow2BlurMax: 0
    readonly property real shadow2Y: 0
    readonly property color shadowPopColor: blackA(2)
    readonly property int shadowPopBlurMax: 12
    readonly property real shadowPopY: 4
    readonly property color glowColor: clear(accent)
    readonly property int glowBlurMax: 0
    readonly property real glowY: 0

    // Artwork scenes keep a dark Radix palette independent of page mode.
    readonly property color wallBg: customized ? wallAccentScale[0] : wallScale[0]
    readonly property color wallSurface: wallScale[1]
    readonly property color wallInk: wallScale[11]
    readonly property color wallInkSecondary: wallScale[10]
    readonly property color wallInkMuted: wallScale[10]
    readonly property color wallInkDim: wallScale[10]
    readonly property color wallMuted: wallScale[10]
    readonly property color wallBorder: wallScale[5]
    readonly property color wallBorderStrong: wallScale[6]
    readonly property color wallCurrentBorder: wallAccentScale[7]
    readonly property color wallCurrentBg: wallAccentScale[4]
    readonly property color wallGlassBg: wallScale[1]
    readonly property color wallBadgeBg: blackA(10)
    readonly property color wallHover: wallScale[3]
    readonly property color wallSoftBg: wallScale[2]
    readonly property color wallPosterHover: whiteA(1)
    readonly property color wallControlHover: wallScale[3]
    readonly property color wallCaptionShadow: blackA(7)
    readonly property int wallCaptionBlur: 0
    readonly property color wallGlassShadow: blackA(2)
    readonly property int wallGlassBlur: 12
    readonly property color wallHeaderShadow: blackA(9)
    readonly property color wallLyricShadow: blackA(7)
    readonly property real wallControlWidth: 36
    readonly property real wallTitleClearance: 116
    readonly property color radioAccent: accentText
    readonly property color radioSoft: accentSoft
    readonly property int wallBarH: 44
    readonly property color wallChromeSurface: wallSurface
    readonly property color wallChromeInk: wallInk
    readonly property color wallChromeMuted: wallInkSecondary
    readonly property color wallChromeHover: wallScale[3]
    readonly property color wallChromePressed: wallScale[4]
    readonly property color wallDanger: radix("red",11,true)
    readonly property color wallAccent: wallAccentScale[8]
    readonly property color immersivePlayBg: wallAccent
    readonly property color immersivePlayFg: inkOn(immersivePlayBg)
    readonly property color immersiveProgress: wallAccent
    readonly property color wallFocus: wallAccentScale[7]
    function luminance(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b)
    }
    function contrast(a, b) {
        var composite = Qt.rgba(a.r*a.a+b.r*(1-a.a), a.g*a.a+b.g*(1-a.a), a.b*a.a+b.b*(1-a.a), 1)
        var x = luminance(composite), y = luminance(b)
        return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05)
    }
    function inkOn(c) { return contrast(black,c) >= contrast(white,c) ? black : white }
    function actionShade(c, amount) { return dark || c.toString() === wallAccent.toString() ? wallAccentScale[9] : accentScale[9] }
    readonly property var mediaStyle: ({
        play: {glyph:"transport-play",dx:0.5,stroke:1.6,stroked:false},
        pause: {glyph:"transport-pause",dx:0,stroke:1.6,stroked:false},
        prev: {glyph:"transport-prev",dx:0,stroke:1.6,stroked:false},
        next: {glyph:"transport-next",dx:0,stroke:1.6,stroked:false}
    })
    function glyphFit(glyph, box, ratio) {
        var r = ratio > 0 ? ratio : dpr
        var g = Math.round(glyph*r), b = Math.round(box*r)
        if ((b-g)%2) g--
        return Math.max(0,g)/r
    }
    function mediaOffset(kind, glyph) { return snap((mediaStyle[kind] || mediaStyle.play).dx * glyph / 24) }
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
    function artwork(url, pixels) {
        return (url || "").replace(/\/stdmusic\/\d+\//, "/stdmusic/" + pixels + "/")
    }
    function displayLg(vw) { return fs2xl }
}
