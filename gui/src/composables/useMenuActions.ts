import { nextTick } from "vue";
import { pickDirectory } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { invokeActivityRunner } from "@/composables/useActivityRunners";
import type { ActivityId } from "@/utils/activities";
import type { MenuAction } from "@/utils/topMenus";

export function useMenuActions() {
  const ui = useUiStore();
  const repo = useRepoStore();

  async function runMenuAction(item: { action?: MenuAction }) {
    const action = item.action;
    if (!action) return;

    switch (action) {
      case "open-repo": {
        try {
          const path = await pickDirectory();
          if (path) await repo.open(path);
        } catch (e) {
          ui.pushLog(String(e), "error");
        }
        break;
      }
      case "close-repo":
        if (repo.isOpen) await repo.close();
        break;
      case "init-repo":
        ui.setActivity("repo");
        break;
      case "run-backup": {
        ui.setActivity("backup");
        await nextTick();
        if (!(await invokeActivityRunner("backup-run"))) {
          ui.pushLog("请在备份页选择源目录后运行", "meta");
        }
        break;
      }
      case "verify": {
        ui.setActivity("verify");
        await nextTick();
        if (!(await invokeActivityRunner("verify-run"))) {
          ui.pushLog("验证页未就绪", "meta");
        }
        break;
      }
      case "recover": {
        ui.setActivity("verify");
        await nextTick();
        if (!(await invokeActivityRunner("recover-run"))) {
          ui.pushLog("修复页未就绪", "meta");
        }
        break;
      }
      case "open-help":
        ui.openHelp("quickstart");
        break;
      case "toggle-settings":
        ui.openSettings();
        break;
      case "toggle-sidebar":
        ui.showSidebar = !ui.showSidebar;
        ui.persistSidebar();
        ui.schedulePersist();
        break;
      case "toggle-log":
        ui.toggleLogCollapsed();
        break;
      case "goto-repo":
        ui.setActivity("repo");
        break;
      case "goto-backup":
        ui.setActivity("backup");
        break;
      case "goto-snapshots":
        ui.setActivity("snapshots");
        break;
      case "goto-restore":
        ui.setActivity("restore");
        break;
      case "goto-verify":
        ui.setActivity("verify");
        break;
      case "goto-maintenance":
        ui.setActivity("maintenance");
        break;
    }
  }

  function gotoActivity(id: ActivityId) {
    ui.setActivity(id);
  }

  return { runMenuAction, gotoActivity };
}
