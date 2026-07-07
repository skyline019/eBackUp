import { pickDirectory } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
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
      case "run-backup":
        ui.setActivity("backup");
        break;
      case "verify":
        ui.setActivity("verify");
        break;
      case "recover":
        ui.setActivity("verify");
        break;
      case "toggle-settings":
        ui.openSettings();
        break;
      case "toggle-sidebar":
        ui.showSidebar = !ui.showSidebar;
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
