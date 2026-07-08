<script setup lang="ts">
import { computed } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { useTaskStore } from "@/stores/taskStore";
import type { LogKind } from "@/stores/uiStore";
import OpacityRegulator from "@/components/OpacityRegulator.vue";
import BackupReportPanel from "@/components/BackupReportPanel.vue";
import { ElMessage } from "element-plus";

const ui = useUiStore();
const task = useTaskStore();

const tabs = [
  { id: "messages" as const, label: "消息" },
  { id: "results" as const, label: "结果" },
  { id: "acceptance" as const, label: "验收报告" },
  { id: "backupReport" as const, label: "备份报告" },
  { id: "audit" as const, label: "审计链" },
  { id: "task" as const, label: "任务" },
];

const visibleLogs = computed(() => [...ui.visibleLogs].reverse().slice(0, 200));

const filterOptions: { value: "all" | LogKind; label: string }[] = [
  { value: "all", label: "全部" },
  { value: "cmd", label: "命令" },
  { value: "success", label: "成功" },
  { value: "error", label: "错误" },
  { value: "meta", label: "系统" },
];

const resultText = computed(() => {
  switch (ui.outputTab) {
    case "results":
      return ui.lastResultJson;
    case "acceptance":
      return ui.lastAcceptanceJson;
    case "backupReport":
      return ui.lastBackupReportJson;
    case "audit":
      return ui.lastAuditJson;
    case "task":
      return ui.lastTaskJson;
    default:
      return "";
  }
});

async function copyLogs() {
  const text = ui.visibleLogs.map((l) => `[${l.time}] ${l.text}`).join("\n");
  try {
    await navigator.clipboard.writeText(text);
    ElMessage.success("日志已复制");
  } catch {
    ElMessage.error("复制失败");
  }
}

async function copyResult() {
  if (!resultText.value) return;
  try {
    await navigator.clipboard.writeText(resultText.value);
    ElMessage.success("已复制到剪贴板");
  } catch {
    ElMessage.error("复制失败");
  }
}
</script>

<template>
  <section class="output-panel console" :class="{ collapsed: ui.settings.logCollapsed }">
    <header class="output-header console-header">
      <div class="output-tabs">
        <button
          v-for="t in tabs"
          :key="t.id"
          type="button"
          class="output-tab"
          :class="{ active: ui.outputTab === t.id }"
          @click="ui.outputTab = t.id"
        >
          {{ t.label }}
        </button>
      </div>
      <div class="console-toolbar">
        <template v-if="ui.outputTab === 'messages'">
          <el-select v-model="ui.logFilterKind" size="small" style="width: 88px">
            <el-option
              v-for="o in filterOptions"
              :key="o.value"
              :label="o.label"
              :value="o.value"
            />
          </el-select>
          <el-input
            v-model="ui.logKeyword"
            size="small"
            placeholder="筛选日志"
            clearable
            style="width: 140px"
          />
          <el-button size="small" text @click="ui.clearLogs()">清空</el-button>
          <el-button size="small" text @click="copyLogs">复制</el-button>
        </template>
        <el-button
          v-else
          size="small"
          text
          :disabled="!resultText"
          @click="copyResult"
        >
          复制 JSON
        </el-button>
        <span class="console-shortcuts">Ctrl+J 折叠 · F1 帮助</span>
        <OpacityRegulator
          class="output-opacity-regulator"
          setting-key="logPanelOpacity"
          label="输出"
          compact
        />
        <el-button size="small" text @click="ui.toggleLogCollapsed()">
          {{ ui.settings.logCollapsed ? "展开" : "折叠" }}
        </el-button>
      </div>
    </header>
    <div v-if="!ui.settings.logCollapsed" class="output-body">
      <div v-if="task.isRunning" class="output-task-strip">
        <span class="ots-label">{{ task.active?.label }}</span>
        <el-progress
          :percentage="task.active?.percent ?? 0"
          :stroke-width="6"
          striped
          striped-flow
          style="flex: 1"
        />
        <span class="ots-phase">{{ task.active?.message }}</span>
      </div>
      <div v-if="ui.outputTab === 'messages'" class="log-list">
        <div
          v-for="(ln, i) in visibleLogs"
          :key="i"
          class="log-line"
          :class="`kind-${ln.kind}`"
        >
          <span class="log-time">{{ ln.time }}</span>
          {{ ln.text }}
        </div>
        <p v-if="!visibleLogs.length" class="muted">暂无消息</p>
      </div>
      <pre v-else-if="ui.outputTab === 'results'" class="json-pane">{{
        ui.lastResultJson || "（无任务结果）"
      }}</pre>
      <pre v-else-if="ui.outputTab === 'acceptance'" class="json-pane">{{
        ui.lastAcceptanceJson || "（无验收报告）"
      }}</pre>
      <BackupReportPanel
        v-else-if="ui.outputTab === 'backupReport'"
        :json-text="ui.lastBackupReportJson"
      />
      <pre v-else-if="ui.outputTab === 'audit'" class="json-pane">{{
        ui.lastAuditJson || "（无审计数据）"
      }}</pre>
      <pre v-else class="json-pane">{{ ui.lastTaskJson || "（无任务详情）" }}</pre>
    </div>
  </section>
</template>

<style scoped>
.output-panel {
  border-top: 1px solid var(--shell-line);
  background: var(--log-panel-surface, rgba(7, 14, 31, 0.88));
  min-height: 140px;
  max-height: 280px;
  display: flex;
  flex-direction: column;
}
.output-panel.collapsed {
  min-height: 36px;
  max-height: 36px;
}
.output-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 4px 8px;
  gap: 8px;
  flex-wrap: wrap;
  background: transparent;
}
.output-tabs {
  display: flex;
  gap: 4px;
}
.output-tab {
  border: none;
  background: transparent;
  color: var(--text-soft);
  padding: 4px 10px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 12px;
}
.output-tab.active {
  background: var(--active-bg);
  color: var(--accent);
}
.output-body {
  flex: 1;
  overflow: auto;
  padding: 8px 12px;
  font-size: 12px;
  background: transparent;
}
.output-task-strip {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 8px;
  padding: 6px 8px;
  border-radius: 6px;
  background: color-mix(in srgb, var(--accent) 10%, transparent);
  border: 1px solid color-mix(in srgb, var(--accent) 25%, var(--shell-line));
}
.ots-label {
  font-size: 11px;
  font-weight: 600;
  color: var(--text-main);
  white-space: nowrap;
}
.ots-phase {
  font-size: 10px;
  color: var(--text-soft);
  white-space: nowrap;
  max-width: 120px;
  overflow: hidden;
  text-overflow: ellipsis;
}
.log-list {
  font-family: var(--log-font-family, monospace);
}
.log-line {
  padding: 2px 0;
  color: var(--text-regular);
}
.log-time {
  opacity: 0.6;
  margin-right: 8px;
}
.kind-error {
  color: #fca5a5;
}
.kind-success {
  color: #86efac;
}
.kind-cmd {
  color: #93c5fd;
}
.json-pane {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-word;
  color: var(--text-regular);
  font-family: var(--log-font-family, monospace);
}
.muted {
  color: var(--text-soft);
}
.console-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
}
.console-shortcuts {
  font-size: 11px;
  color: var(--text-soft);
}
</style>
