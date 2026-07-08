<script setup lang="ts">
import { computed } from "vue";
import { BUILTIN_PLUGINS, type BackupReportDto, type PluginReportDto } from "@/api/ebbackup";

const props = defineProps<{
  jsonText: string;
}>();

const REASON_LABELS: Record<string, string> = {
  locked: "文件被锁定",
  permission_denied: "权限不足",
  unreadable: "不可读",
  depth_exceeded: "目录过深",
  symlink_loop: "符号链接环",
  reparse_junction: "联接点（未展开）",
  window_truncated: "备份窗口截断",
};

const PLUGIN_NAME_BY_ID = Object.fromEntries(
  BUILTIN_PLUGINS.map((p) => [p.id, p.label]),
);

function reasonLabel(reason: string): string {
  if (reason.startsWith("hook_failed:")) return "Hook 失败";
  if (reason.startsWith("plugin_skipped:platform:")) {
    const id = reason.split(":").pop() ?? "";
    const name = PLUGIN_NAME_BY_ID[id] ?? id;
    return `插件跳过（平台不支持：${name}）`;
  }
  if (reason.startsWith("plugin_quiesce_failed:")) return "插件 quiesce 失败";
  if (reason.startsWith("plugin_unknown:")) return "未知插件";
  return REASON_LABELS[reason] ?? reason;
}

function pluginDisplayName(row: PluginReportDto): string {
  return PLUGIN_NAME_BY_ID[row.id] ?? row.id;
}

function pluginMetricCells(row: PluginReportDto): Array<{ label: string; value: number | string }> {
  const cells: Array<{ label: string; value: number | string }> = [];
  if (row.checkpointed != null) cells.push({ label: "checkpoint", value: row.checkpointed });
  if (row.exported != null) cells.push({ label: "exported", value: row.exported });
  if (row.mounted != null) cells.push({ label: "mounted", value: row.mounted });
  if (row.failed != null) cells.push({ label: "failed", value: row.failed });
  if (row.note != null) cells.push({ label: "note", value: String(row.note) });
  return cells;
}

const report = computed((): BackupReportDto | null => {
  if (!props.jsonText.trim()) return null;
  try {
    return JSON.parse(props.jsonText) as BackupReportDto;
  } catch {
    return null;
  }
});

const issues = computed(() => report.value?.issues ?? []);
const plugins = computed(() => report.value?.plugins ?? []);

function formatUnix(ts?: number) {
  if (!ts) return "";
  return new Date(ts * 1000).toLocaleString();
}
</script>

<template>
  <div v-if="!report" class="backup-report-panel muted">（无备份报告）</div>
  <div v-else class="backup-report-panel">
    <div class="summary-grid">
      <div><span class="label">Txn</span> {{ report.txn_id }}</div>
      <div v-if="report.job_id"><span class="label">作业</span> {{ report.job_id }}</div>
      <div v-if="report.retention_tag"><span class="label">保留标签</span> {{ report.retention_tag }}</div>
      <div v-if="report.immutable_until_unix">
        <span class="label">不可变至</span> {{ formatUnix(report.immutable_until_unix) }}
      </div>
      <div><span class="label">已备份</span> {{ report.backed_up }}</div>
      <div><span class="label">锁定</span> {{ report.locked ?? 0 }}</div>
      <div><span class="label">权限</span> {{ report.permission_denied ?? 0 }}</div>
      <div><span class="label">跳过</span> {{ report.skipped ?? 0 }}</div>
      <div v-if="(report.reparse_junction ?? 0) > 0">
        <span class="label">联接点</span> {{ report.reparse_junction }}
      </div>
      <div v-if="(report.hook_failed ?? 0) > 0">
        <span class="label">Hook</span> {{ report.hook_failed }}
      </div>
      <div v-if="(report.plugin_skipped ?? 0) > 0">
        <span class="label">插件跳过</span> {{ report.plugin_skipped }}
      </div>
      <div v-if="(report.plugin_failed ?? 0) > 0">
        <span class="label">插件失败</span> {{ report.plugin_failed }}
      </div>
      <div v-if="report.durability_downgraded">
        <span class="label">Durability</span> 已降级 Balanced
      </div>
      <div v-if="report.window_truncated">
        <span class="label">窗口</span> 已截断（部分快照）
      </div>
      <div v-if="report.window_end_unix">
        <span class="label">窗口截止</span> {{ formatUnix(report.window_end_unix) }}
      </div>
      <div><span class="label">复用率</span> {{ Math.round(report.reuse_pct ?? 0) }}%</div>
    </div>

    <table v-if="plugins.length" class="issues-table plugins-table">
      <thead>
        <tr>
          <th>插件</th>
          <th>摘要</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="(row, i) in plugins" :key="i">
          <td>{{ pluginDisplayName(row) }}</td>
          <td>
            <span v-for="(cell, j) in pluginMetricCells(row)" :key="j" class="metric-chip">
              {{ cell.label }}={{ cell.value }}
            </span>
          </td>
        </tr>
      </tbody>
    </table>

    <table v-if="issues.length" class="issues-table">
      <thead>
        <tr>
          <th>路径</th>
          <th>原因</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="(row, i) in issues" :key="i">
          <td class="path-cell">{{ row.path || "（全局）" }}</td>
          <td>{{ reasonLabel(row.reason) }}</td>
        </tr>
      </tbody>
    </table>
    <p v-else class="muted ok-line">本次备份无未完全备份项。</p>
  </div>
</template>

<style scoped>
.backup-report-panel {
  padding: 8px 12px;
  font-size: 12px;
  overflow: auto;
  height: 100%;
}
.summary-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(100px, 1fr));
  gap: 6px 12px;
  margin-bottom: 10px;
}
.label {
  color: var(--shell-muted, #8892a6);
  margin-right: 4px;
}
.issues-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 11px;
  margin-bottom: 10px;
}
.issues-table th,
.issues-table td {
  border: 1px solid var(--shell-line, #2a3348);
  padding: 4px 6px;
  text-align: left;
}
.path-cell {
  word-break: break-all;
  font-family: var(--mono-font, monospace);
}
.metric-chip {
  display: inline-block;
  margin-right: 8px;
  font-family: var(--mono-font, monospace);
}
.muted {
  color: var(--shell-muted, #8892a6);
}
.ok-line {
  margin: 0;
}
</style>
