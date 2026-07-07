import { onMounted, onBeforeUnmount } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { type ActivityId } from "@/utils/activities";

const ACTIVITY_KEYS: Record<string, ActivityId> = {
  "1": "repo",
  "2": "backup",
  "3": "snapshots",
  "4": "restore",
  "5": "verify",
  "6": "maintenance",
};

export function useGlobalShortcuts() {
  const ui = useUiStore();

  function onGlobalKey(ev: KeyboardEvent) {
    const target = ev.target as HTMLElement | null;
    const tag = target?.tagName?.toLowerCase() ?? "";
    const inInput =
      tag === "input" || tag === "textarea" || target?.isContentEditable;

    if (ev.key === "F1") {
      ev.preventDefault();
      ui.openHelp("quickstart");
      return;
    }

    if (ev.ctrlKey && !ev.altKey) {
      const key = ev.key.toLowerCase();
      if (key === "b") {
        ev.preventDefault();
        ui.showSidebar = !ui.showSidebar;
        ui.persistSidebar();
        ui.schedulePersist();
        return;
      }
      if (key === "j" || key === "`") {
        ev.preventDefault();
        ui.toggleLogCollapsed();
        return;
      }
      if (key === ",") {
        ev.preventDefault();
        ui.openSettings();
        return;
      }
      if (key === "/" || (key === "?" && ev.shiftKey)) {
        ev.preventDefault();
        ui.openHelp("faq");
        return;
      }
      if (!ev.shiftKey && ACTIVITY_KEYS[key]) {
        ev.preventDefault();
        ui.setActivity(ACTIVITY_KEYS[key]);
        return;
      }
    }

    if (!inInput && ev.ctrlKey && ev.shiftKey && ev.key.toLowerCase() === "p") {
      ev.preventDefault();
      ui.openHelp("keys");
    }
  }

  onMounted(() => window.addEventListener("keydown", onGlobalKey));
  onBeforeUnmount(() => window.removeEventListener("keydown", onGlobalKey));
}

export const SHORTCUT_HELP = [
  { keys: "Ctrl+1 … Ctrl+6", desc: "切换活动（仓库 / 备份 / 快照 / 恢复 / 验证 / 维护）" },
  { keys: "Ctrl+B", desc: "显示或隐藏侧栏" },
  { keys: "Ctrl+J / Ctrl+`", desc: "折叠或展开底部输出面板" },
  { keys: "Ctrl+,", desc: "打开外观设置" },
  { keys: "F1", desc: "帮助中心（快速入门）" },
  { keys: "Ctrl+/", desc: "帮助中心（常见问题）" },
  { keys: "Ctrl+Shift+P", desc: "帮助中心（快捷键列表）" },
];
