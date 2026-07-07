import { onMounted, onBeforeUnmount } from "vue";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { useTaskStore } from "@/stores/taskStore";
import { useUiStore } from "@/stores/uiStore";
import type { TaskKind } from "@/utils/taskPhases";
import { isTauriRuntime } from "@/utils/tauriRuntime";

interface TaskEventPayload {
  kind: string;
  permille?: number;
  ok?: boolean;
}

export function useTaskEvents() {
  const task = useTaskStore();
  const ui = useUiStore();
  let unsubs: UnlistenFn[] = [];

  onMounted(async () => {
    if (!isTauriRuntime()) return;
    unsubs.push(
      await listen<TaskEventPayload>("task-started", (ev) => {
        const kind = ev.payload.kind as TaskKind;
        task.start(kind);
        ui.settings.logCollapsed = false;
        ui.schedulePersist();
        ui.outputTab = "messages";
      })
    );
    unsubs.push(
      await listen<TaskEventPayload>("task-progress", (ev) => {
        if (typeof ev.payload.permille === "number") {
          task.updatePermille(ev.payload.permille);
        }
      })
    );
    unsubs.push(
      await listen<TaskEventPayload>("task-finished", (ev) => {
        task.finish(ev.payload.ok !== false);
      })
    );
  });

  onBeforeUnmount(() => {
    for (const u of unsubs) u();
    unsubs = [];
  });
}
