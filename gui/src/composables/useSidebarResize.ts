import { ref } from "vue";
import { useUiStore } from "@/stores/uiStore";

export function useSidebarResize() {
  const ui = useUiStore();
  const sidebarResizing = ref(false);
  let dragStartX = 0;
  let dragStartWidth = 260;

  function onResizeMove(ev: MouseEvent) {
    if (!sidebarResizing.value) return;
    const next = Math.max(
      200,
      Math.min(460, dragStartWidth + (ev.clientX - dragStartX))
    );
    if (ui.settings.sidebarWidth !== next) {
      ui.settings.sidebarWidth = next;
      ui.applyThemeVars();
    }
  }

  function stopResize() {
    if (!sidebarResizing.value) return;
    sidebarResizing.value = false;
    document.removeEventListener("mousemove", onResizeMove);
    document.removeEventListener("mouseup", stopResize);
    ui.save();
  }

  function startResize(ev: MouseEvent) {
    sidebarResizing.value = true;
    dragStartX = ev.clientX;
    dragStartWidth = ui.settings.sidebarWidth;
    document.addEventListener("mousemove", onResizeMove);
    document.addEventListener("mouseup", stopResize);
  }

  return { sidebarResizing, startResize };
}
