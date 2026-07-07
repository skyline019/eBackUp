import { ref } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { ElMessage } from "element-plus";

export function useWallpaperFiles() {
  const ui = useUiStore();
  const bgImageInput = ref<HTMLInputElement | null>(null);
  const bgVideoInput = ref<HTMLInputElement | null>(null);

  function pickBackgroundImage() {
    bgImageInput.value?.click();
  }

  function pickBackgroundVideo() {
    bgVideoInput.value?.click();
  }

  function onBackgroundImageChange(ev: Event) {
    const input = ev.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;
    if (!file.type.startsWith("image/")) {
      ElMessage.warning("请选择图片文件（png/jpg/webp 等）");
      input.value = "";
      return;
    }
    const reader = new FileReader();
    reader.onload = () => {
      ui.settings.bgMode = "image";
      ui.settings.bgImageUrl = String(reader.result ?? "");
      ui.applyThemeVars();
      ui.schedulePersist();
    };
    reader.readAsDataURL(file);
    input.value = "";
  }

  function onBackgroundVideoChange(ev: Event) {
    const input = ev.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;
    if (!file.type.startsWith("video/")) {
      ElMessage.warning("请选择视频文件（mp4/webm/ogg 等）");
      input.value = "";
      return;
    }
    const reader = new FileReader();
    reader.onload = () => {
      ui.settings.bgMode = "video";
      ui.settings.bgVideoUrl = String(reader.result ?? "");
      ui.applyThemeVars();
      ui.schedulePersist();
    };
    reader.readAsDataURL(file);
    input.value = "";
  }

  function clearWallpaper() {
    ui.settings.bgImageUrl = "";
    ui.settings.bgVideoUrl = "";
    ui.settings.bgMode = "gradient";
    ui.applyThemeVars();
    ui.schedulePersist();
  }

  return {
    bgImageInput,
    bgVideoInput,
    pickBackgroundImage,
    pickBackgroundVideo,
    onBackgroundImageChange,
    onBackgroundVideoChange,
    clearWallpaper,
  };
}
