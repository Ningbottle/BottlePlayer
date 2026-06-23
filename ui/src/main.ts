import { createApp } from "vue";
import App from "./App.vue";
import "./style.css";
import { useThemeStore } from "./api/themeStore";

useThemeStore().init();

createApp(App).mount("#app");
