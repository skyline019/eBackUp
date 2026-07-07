<script setup lang="ts">
import { computed } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { SHORTCUT_HELP } from "@/composables/useGlobalShortcuts";

const ui = useUiStore();

const visible = computed({
  get: () => ui.showShortcutHelp,
  set: (v: boolean) => {
    ui.showShortcutHelp = v;
  },
});
</script>

<template>
  <el-dialog
    v-model="visible"
    title="快捷键（完整列表见帮助中心 F1）"
    width="520px"
    align-center
    append-to-body
    class="shortcut-help-dialog"
  >
    <div class="shortcut-grid">
      <template v-for="(row, i) in SHORTCUT_HELP" :key="i">
        <span>{{ row.desc }}</span>
        <kbd>{{ row.keys }}</kbd>
      </template>
    </div>
    <template #footer>
      <el-button @click="ui.openHelp('keys')">打开帮助中心</el-button>
      <el-button type="primary" @click="visible = false">关闭</el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.shortcut-grid {
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 10px 20px;
  font-size: 13px;
}
.shortcut-grid kbd {
  font-family: var(--font-mono);
  font-size: 11px;
  padding: 3px 8px;
  border-radius: 4px;
  border: 1px solid var(--shell-line);
  background: color-mix(in srgb, var(--panel-bg-strong) 80%, transparent);
  color: var(--text-main);
  white-space: nowrap;
}
</style>
