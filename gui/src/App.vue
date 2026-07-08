<script setup lang="ts">
import { computed, onMounted } from "vue";
import AppShell from "@/shell/AppShell.vue";
import ShortcutHelp from "@/shell/ShortcutHelp.vue";
import HelpCenter from "@/shell/HelpCenter.vue";
import SettingsDrawer from "@/shell/SettingsDrawer.vue";
import { useUiStore } from "@/stores/uiStore";
import { useRepoStore } from "@/stores/repoStore";
import { useGlobalShortcuts } from "@/composables/useGlobalShortcuts";
import { useTaskEvents } from "@/composables/useTaskEvents";
import { useBackupAlerts } from "@/composables/useBackupAlerts";
import { useProfileStore } from "@/stores/profileStore";
import { isTauriRuntime } from "@/utils/tauriRuntime";

const ui = useUiStore();
const repo = useRepoStore();
const profile = useProfileStore();
useGlobalShortcuts();
useTaskEvents();
useBackupAlerts();

const runtimeHint = computed(() =>
  isTauriRuntime()
    ? ""
    : "当前为浏览器预览模式：仅界面/设置可用；打开仓库与备份请使用 npm run tauri:dev"
);

const settingsOpen = computed({
  get: () => ui.showSettings,
  set: (v: boolean) => {
    ui.setShowSettings(v);
  },
});

onMounted(async () => {
  await profile.load();
  await profile.ensureActive();
  await ui.load(profile.activeProfileId);
  await repo.loadLocal(profile.activeProfileId);
  ui.pushLog("ebbackup Workbench 已启动 — F1 帮助中心", "meta");
  if (!isTauriRuntime()) {
    ui.pushLog(runtimeHint.value, "error");
  }
});
</script>

<template>
  <div v-if="runtimeHint" class="runtime-banner">{{ runtimeHint }}</div>
  <AppShell />
  <ShortcutHelp />
  <HelpCenter />
  <SettingsDrawer v-model="settingsOpen" />
</template>

<style scoped>
.runtime-banner {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  z-index: 20000;
  padding: 6px 12px;
  font-size: 12px;
  text-align: center;
  color: #fef3c7;
  background: #92400e;
  border-bottom: 1px solid #f59e0b;
  pointer-events: none;
}
</style>
