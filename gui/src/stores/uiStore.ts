import { defineStore } from "pinia";
import {
  defaultUiSettings,
  layoutStyleFromSettings,
  sanitizeUiSettings,
  type UiSettings,
} from "@/utils/themeSettings";
import { hasActiveWallpaper } from "@/utils/themeSettings";
import {
  getUiSettings,
  setUiSettings,
  uiSettingsExists,
  uiSettingsPath,
} from "@/api/ebbackup";
import { isTauriRuntime } from "@/utils/tauriRuntime";
import type { ActivityId } from "@/utils/activities";

const LS_LOGS = "ebbackup_workbench_logs";
const LS_UI = "ebbackup_workbench_ui";

export type LogKind = "cmd" | "error" | "success" | "meta";

export interface LogLine {
  text: string;
  kind: LogKind;
  time: string;
}

function nowTime(): string {
  return new Date().toLocaleTimeString();
}

let persistTimer: ReturnType<typeof setTimeout> | null = null;

export const useUiStore = defineStore("ui", {
  state: () => ({
    activity: "repo" as ActivityId,
    showSidebar: true,
    showSettings: false,
    showShortcutHelp: false,
    outputTab: "messages" as "messages" | "results" | "audit" | "task",
    logAutoFollow: true,
    logFilterKind: "all" as "all" | LogKind,
    logKeyword: "",
    logs: [] as LogLine[],
    settings: { ...defaultUiSettings } as UiSettings,
    settingsFilePath: "" as string,
    lastResultJson: "" as string,
    lastTaskJson: "" as string,
    lastAuditJson: "" as string,
  }),
  getters: {
    visibleLogs(state): LogLine[] {
      const kw = state.logKeyword.trim().toLowerCase();
      return state.logs.filter((ln) => {
        if (state.logFilterKind !== "all" && ln.kind !== state.logFilterKind) return false;
        if (kw && !ln.text.toLowerCase().includes(kw)) return false;
        return true;
      });
    },
    layoutStyle(state): Record<string, string> {
      return layoutStyleFromSettings(state.settings);
    },
    layoutClass(state) {
      return {
        dense: state.settings.denseMode,
        "no-anim": !state.settings.animations,
        "sidebar-collapsed": !state.showSidebar,
        "layout-wallpaper-active": hasActiveWallpaper(state.settings),
      };
    },
  },
  actions: {
    async load() {
      try {
        const rawLogs = localStorage.getItem(LS_LOGS);
        if (rawLogs) this.logs = JSON.parse(rawLogs) as LogLine[];

        if (isTauriRuntime()) {
          const [exists, disk, path] = await Promise.all([
            uiSettingsExists(),
            getUiSettings(),
            uiSettingsPath(),
          ]);
          if (path) this.settingsFilePath = path;
          const legacyRaw = localStorage.getItem(LS_UI);
          if (!exists && legacyRaw) {
            this.settings = sanitizeUiSettings({
              ...defaultUiSettings,
              ...(JSON.parse(legacyRaw) as Partial<UiSettings>),
            });
            await this.persistSettingsNow();
            localStorage.removeItem(LS_UI);
          } else if (disk) {
            this.settings = sanitizeUiSettings({ ...defaultUiSettings, ...disk });
          }
        } else {
          const rawUi = localStorage.getItem(LS_UI);
          if (rawUi) {
            this.settings = sanitizeUiSettings({
              ...defaultUiSettings,
              ...(JSON.parse(rawUi) as Partial<UiSettings>),
            });
          }
        }
      } catch {
        this.settings = sanitizeUiSettings(defaultUiSettings);
      }
      this.applyThemeVars();
    },
    save() {
      void this.persistSettingsNow();
      localStorage.setItem(LS_LOGS, JSON.stringify(this.logs.slice(-500)));
      this.applyThemeVars();
    },
    schedulePersist(delayMs = 280) {
      if (!isTauriRuntime()) {
        try {
          localStorage.setItem(LS_UI, JSON.stringify(this.settings));
        } catch {
          /* quota */
        }
        this.applyThemeVars();
        return;
      }
      if (persistTimer) clearTimeout(persistTimer);
      persistTimer = setTimeout(() => {
        persistTimer = null;
        void this.persistSettingsNow();
      }, delayMs);
      this.applyThemeVars();
    },
    async persistSettingsNow() {
      this.settings = sanitizeUiSettings(this.settings);
      if (isTauriRuntime()) {
        try {
          await setUiSettings(this.settings);
          if (!this.settingsFilePath) {
            this.settingsFilePath = (await uiSettingsPath()) ?? "";
          }
        } catch (e) {
          this.pushLog(`设置保存失败: ${e}`, "error");
        }
      } else {
        try {
          localStorage.setItem(LS_UI, JSON.stringify(this.settings));
        } catch {
          /* ignore */
        }
      }
    },
    applyThemeVars() {
      const s = sanitizeUiSettings(this.settings);
      const root = document.documentElement;
      const style = layoutStyleFromSettings(s);
      for (const [k, v] of Object.entries(style)) {
        if (k.startsWith("--")) root.style.setProperty(k, v);
      }
      root.dataset.bgMode = s.bgMode;
      root.dataset.wallpaperActive =
        (s.bgMode === "image" && s.bgImageUrl.trim()) ||
        (s.bgMode === "video" && s.bgVideoUrl.trim())
          ? "1"
          : "0";
    },
    applyPreset(partial: Partial<UiSettings>) {
      this.settings = sanitizeUiSettings({ ...this.settings, ...partial });
      this.applyThemeVars();
      this.schedulePersist();
    },
    pushLog(text: string, kind: LogKind = "meta") {
      this.logs.push({ text, kind, time: nowTime() });
      if (this.logs.length > 500) this.logs.splice(0, this.logs.length - 500);
      localStorage.setItem(LS_LOGS, JSON.stringify(this.logs));
    },
    clearLogs() {
      this.logs = [];
      localStorage.removeItem(LS_LOGS);
    },
    toggleLogCollapsed() {
      this.settings.logCollapsed = !this.settings.logCollapsed;
      this.schedulePersist();
    },
    setActivity(id: ActivityId) {
      this.activity = id;
    },
    openSettings() {
      this.showSettings = true;
    },
    closeSettings() {
      this.showSettings = false;
    },
    setShowSettings(v: boolean) {
      this.showSettings = v;
    },
    setTaskResult(obj: unknown) {
      this.lastTaskJson = JSON.stringify(obj, null, 2);
      this.outputTab = "task";
    },
    setAuditResult(obj: unknown) {
      this.lastAuditJson = JSON.stringify(obj, null, 2);
      this.outputTab = "audit";
    },
  },
});
