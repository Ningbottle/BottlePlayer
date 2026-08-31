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
import "./styles/base.css";
import "./app/shell/shell.css";
import "./features/settings/settings.css";
import "./app/shell/pageRecovery.css";
import "./features/lyrics/lyrics.css";
import "./styles/skins/aurora.css";
import "./styles/skins/newsprint.css";
import { useThemeStore } from "./app/appearance/themeStore";
import { useLyricFocusStore } from "./features/lyrics";
import { favoriteStore } from "./features/library";
import { userStore, configureAccountEffects } from "./features/account";
import {
  disposePlayerRuntime,
  recentPlayedStore,
  configurePlayHistoryPolicy,
} from "./playback/index";
import { installPageLifecycle } from "./app/lifecycle/pageLifecycle";
import { router } from "./app/navigation/router";
import { configureMotionProfileProvider } from "./shared/motion/motion";

const themeStore = useThemeStore();
themeStore.init();
useLyricFocusStore().init();

// The neutral motion module reads the live skin through this provider — no
// second skin source of truth, no watcher. Configured before app.mount.
configureMotionProfileProvider(() => themeStore.skinId.value);

// Cross-module wiring lives here and only here: the account store emits
// notifications, the composition root decides what they do (Library
// reconciliation + device-local history reset), and the play-history upload
// policy follows the account's login state.
configureAccountEffects({
  onAccountReady: (userId) => favoriteStore.onLogin(userId),
  onAccountCleared: () => favoriteStore.onLogout(),
  onLocalLogout: () => recentPlayedStore.reset(),
});
configurePlayHistoryPolicy({
  isUploadEnabled: () => userStore.isLoggedIn,
});

// Application composition owns the page lifecycle: the single pagehide
// listener lives in app/lifecycle and calls the player's public shutdown
// command. The Store itself registers no listeners.
installPageLifecycle({
  shutdownPlayback: disposePlayerRuntime,
});

const app = createApp(App);
app.use(router);
app.mount("#app");
