<script setup lang="ts">
import { computed } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { OPACITY_FULL } from "@/utils/themeSettings";

const props = defineProps<{
  settingKey: "workspaceCardOpacity" | "logPanelOpacity";
  label?: string;
  compact?: boolean;
}>();

const ui = useUiStore();

const value = computed({
  get: () => ui.settings[props.settingKey],
  set: (v: number) => {
    ui.settings[props.settingKey] = v;
    ui.applyThemeVars();
    ui.schedulePersist();
  },
});

const pct = computed(() => Math.round(value.value * 100));
</script>

<template>
  <div
    class="opacity-regulator"
    :class="{ compact }"
    title="拖动调节背景不透明度"
    @mousedown.stop
    @click.stop
  >
    <span class="opacity-label">{{ label ?? "透明度" }}</span>
    <el-slider
      v-model="value"
      :min="OPACITY_FULL.min"
      :max="OPACITY_FULL.max"
      :step="0.01"
      :show-tooltip="true"
      :format-tooltip="(v: number) => `${Math.round(v * 100)}%`"
      size="small"
    />
    <span class="opacity-pct">{{ pct }}%</span>
  </div>
</template>

<style scoped>
.opacity-regulator {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 10px;
  border-radius: 8px;
  border: 1px solid color-mix(in srgb, var(--card-border) 70%, transparent);
  background: color-mix(in srgb, var(--panel-bg-strong) 72%, transparent);
  backdrop-filter: blur(6px);
  min-width: 200px;
}
.opacity-regulator.compact {
  min-width: 168px;
  padding: 2px 8px;
  gap: 6px;
}
.opacity-label {
  font-size: 11px;
  color: var(--text-soft);
  white-space: nowrap;
  flex-shrink: 0;
}
.opacity-pct {
  font-size: 11px;
  font-variant-numeric: tabular-nums;
  color: var(--text-regular);
  min-width: 32px;
  text-align: right;
  flex-shrink: 0;
}
.opacity-regulator :deep(.el-slider) {
  flex: 1;
  min-width: 72px;
}
.opacity-regulator :deep(.el-slider__runway) {
  height: 4px;
  background: color-mix(in srgb, var(--text-soft) 22%, transparent);
}
.opacity-regulator :deep(.el-slider__bar) {
  background: var(--accent);
}
.opacity-regulator :deep(.el-slider__button) {
  width: 12px;
  height: 12px;
  border-color: var(--accent);
}
</style>
