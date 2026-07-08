<script setup lang="ts">
import { computed } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { useRepoStore } from "@/stores/repoStore";
import { useMenuActions } from "@/composables/useMenuActions";
import { topMenus, type MenuNode } from "@/utils/topMenus";
import ProfileSwitcher from "@/shell/ProfileSwitcher.vue";
import { RefreshRight, Setting, Upload, QuestionFilled } from "@element-plus/icons-vue";

const ui = useUiStore();
const repo = useRepoStore();
const { runMenuAction } = useMenuActions();

const statusClass = computed(() => (repo.isOpen ? "is-embedded" : "is-offline"));
const statusText = computed(() =>
  repo.isOpen ? `已打开 · ${repo.path}` : "未打开仓库"
);

function onMenuCommand(item: MenuNode) {
  if (item.action) void runMenuAction(item);
}

function flattenMenuItems(items: MenuNode[]): MenuNode[] {
  const out: MenuNode[] = [];
  for (const item of items) {
    if (item.children?.length) {
      out.push(...flattenMenuItems(item.children));
      continue;
    }
    out.push(item);
  }
  return out;
}

async function refreshRepo() {
  if (!repo.isOpen) return;
  const ui = useUiStore();
  try {
    await repo.refreshInfo();
    await repo.refreshSnapshots();
  } catch (e) {
    ui.pushLog(String(e), "error");
  }
}
</script>

<template>
  <header class="ribbon-bar menu-bar">
    <div class="menu-bar-brand">
      <span class="menu-bar-brand-mark">ebbackup Workbench</span>
    </div>

    <nav class="menu-bar-menus" aria-label="主菜单">
      <el-dropdown
        v-for="menu in topMenus"
        :key="menu.key"
        trigger="click"
        :hide-on-click="true"
        @command="onMenuCommand"
      >
        <button type="button" class="menu-title">{{ menu.label }}</button>
        <template #dropdown>
          <el-dropdown-menu>
            <template v-for="(item, idx) in flattenMenuItems(menu.items)" :key="idx">
              <el-dropdown-item v-if="item.divider" :divided="true" disabled>—</el-dropdown-item>
              <el-dropdown-item v-else-if="item.section" disabled>{{ item.label }}</el-dropdown-item>
              <el-dropdown-item v-else :command="item">{{ item.label }}</el-dropdown-item>
            </template>
          </el-dropdown-menu>
        </template>
      </el-dropdown>
    </nav>

    <div class="ribbon-actions">
      <ProfileSwitcher />
      <span class="status-pill" :class="statusClass">
        <span class="status-pill-dot" />
        <span class="status-pill-text">{{ statusText }}</span>
      </span>
      <el-button
        size="small"
        :icon="Upload"
        type="primary"
        :disabled="!repo.isOpen"
        @click="ui.setActivity('backup')"
      >
        备份
      </el-button>
      <el-button
        size="small"
        :icon="RefreshRight"
        title="刷新仓库信息与快照"
        :disabled="!repo.isOpen"
        @click="refreshRepo"
      />
      <el-button size="small" :icon="QuestionFilled" title="帮助中心 (F1)" @click="ui.openHelp('quickstart')" />
      <el-button size="small" :icon="Setting" title="外观设置 (Ctrl+,)" @click="ui.openSettings()" />
    </div>
  </header>
</template>
