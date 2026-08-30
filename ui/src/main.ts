import { createApp } from "vue";
import App from "./App.vue";
// Self-hosted fonts (replace Google Fonts CDN - blocked offline / in CN, and
// EB Garamond / Libre Caslon Display were never loaded at all -> blurry
// substitution on machines without the fonts). Noto Serif SC limited to
// 400/700 to keep the CJK payload bounded.
import "@fontsource/inter/400.css";
import "@fontsource/inter/500.css";
import "@fontsource/inter/600.css";
import "@fontsource/inter/700.css";
import "@fontsource/eb-garamond/400.css";
import "@fontsource/eb-garamond/500.css";
import "@fontsource/eb-garamond/600.css";
import "@fontsource/eb-garamond/700.css";
import "@fontsource/libre-caslon-display/400.css";
import "@fontsource/zcool-xiaowei/400.css";
import "@fontsource/noto-serif-sc/400.css";
import "@fontsource/noto-serif-sc/700.css";
import "./styles/tokens.css";
import "./styles/progress.css";
import "./style.css";
import "./styles/skins/aurora.css";
import "./styles/skins/newsprint.css";
import { useThemeStore } from "./api/themeStore";
import { useLyricFocusStore } from "./api/lyricFocusStore";
import { disposePlayerRuntime } from "./playback/playerStore";
import { installPageLifecycle } from "./app/lifecycle/pageLifecycle";
import { router } from "./navigation/router";
import { configureMotionProfileProvider } from "./shared/motion/motion";

const themeStore = useThemeStore();
themeStore.init();
useLyricFocusStore().init();

// The neutral motion module reads the live skin through this provider — no
// second skin source of truth, no watcher. Configured before app.mount.
configureMotionProfileProvider(() => themeStore.skinId.value);

// Application composition owns the page lifecycle: the single pagehide
// listener lives in app/lifecycle and calls the player's public shutdown
// command. The Store itself registers no listeners.
installPageLifecycle({
  shutdownPlayback: disposePlayerRuntime,
});

const app = createApp(App);
app.use(router);
app.mount("#app");
