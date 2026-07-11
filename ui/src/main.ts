import { createApp } from "vue";
import App from "./App.vue";
import "./styles/tokens.css";
import "./styles/progress.css";
import "./style.css";
import "./styles/skins/aurora.css";
import "./styles/skins/newsprint.css";
import { useThemeStore } from "./api/themeStore";

useThemeStore().init();

createApp(App).mount("#app");
