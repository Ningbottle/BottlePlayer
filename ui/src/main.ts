import { createApp } from "vue";
import App from "./App.vue";
import "./styles/tokens.css";
import "./styles/progress.css";
import "./style.css";
import "./styles/skins/aurora.css";
import "./styles/skins/newsprint.css";
import { useThemeStore } from "./api/themeStore";
import { useLyricFocusStore } from "./api/lyricFocusStore";
import { router } from "./navigation/router";

useThemeStore().init();
useLyricFocusStore().init();

const app = createApp(App);
app.use(router);
app.mount("#app");
