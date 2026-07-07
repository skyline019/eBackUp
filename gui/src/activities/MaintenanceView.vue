<script setup lang="ts">
import { onMounted, ref } from "vue";
import { compactRepo, gcOrphans } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { confirmDestructive } from "@/utils/confirmDestructive";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";

const repo = useRepoStore();
const ui = useUiStore();

const compactDryRun = ref(true);
const gcDryRun = ref(true);
const busy = ref(false);
const lastCompactSummary = ref("");
const lastGcSummary = ref("");

onMounted(async () => {
  if (repo.isOpen) await repo.refreshInfo();
});

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MiB`;
  return `${(n / 1024 / 1024 / 1024).toFixed(2)} GiB`;
}

function summarizeCompact(res: Record<string, unknown>) {
  const before = Number(res.physical_before ?? 0);
  const after = Number(res.physical_after ?? 0);
  const ratio = Number(res.ampl_ratio ?? 0);
  return `物理 ${formatBytes(before)} → ${formatBytes(after)}，放大率 ${ratio.toFixed(2)}×`;
}

function summarizeGc(res: Record<string, unknown>) {
  const reclaimed = Number(res.bytes_reclaimed ?? res.reclaimed_bytes ?? 0);
  const count = Number(res.chunks_removed ?? res.removed_count ?? 0);
  return `回收 ${formatBytes(reclaimed)}，移除 ${count} 块`;
}

async function refreshStats() {
  try {
    await repo.refreshInfo();
    ui.pushLog("统计已刷新", "meta");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e)), "error");
  }
}

async function runCompact() {
  if (!repo.isOpen) return;
  if (!compactDryRun.value) {
    const ok = await confirmDestructive(
      "执行 Compact",
      "将重写 EbPack 布局以降低放大率，耗时与仓库大小相关。建议先 Dry run 查看预估。"
    );
    if (!ok) return;
  }
  busy.value = true;
  try {
    const res = await compactRepo(compactDryRun.value);
    ui.setTaskResult(res);
    lastCompactSummary.value = summarizeCompact(res);
    ui.pushLog(`Compact ${compactDryRun.value ? "(dry-run) " : ""}${lastCompactSummary.value}`, "success");
    if (!compactDryRun.value) await repo.refreshInfo();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "Compact 失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function runGc() {
  if (!repo.isOpen) return;
  if (!gcDryRun.value) {
    const ok = await confirmDestructive(
      "执行孤儿块 GC",
      "将删除无引用的 chunk 数据。请确认已备份且了解回收范围。"
    );
    if (!ok) return;
  }
  busy.value = true;
  try {
    const res = await gcOrphans(gcDryRun.value);
    ui.setTaskResult(res);
    lastGcSummary.value = summarizeGc(res);
    ui.pushLog(`GC ${gcDryRun.value ? "(dry-run) " : ""}${lastGcSummary.value}`, "success");
    if (!gcDryRun.value) await repo.refreshInfo();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "GC 失败"), "error");
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <div class="activity-page">
    <section v-if="repo.info" class="panel-card stats-grid">
      <div class="head">
        <h2>仓库统计</h2>
        <div class="head-actions">
          <el-button link type="primary" @click="ui.openHelp('activity')">帮助</el-button>
          <el-button size="small" @click="refreshStats">刷新</el-button>
        </div>
      </div>
      <div class="cards">
        <div class="stat-card">
          <span class="label">物理占用</span>
          <span class="value">{{ formatBytes(repo.info.physical_bytes) }}</span>
        </div>
        <div class="stat-card">
          <span class="label">有效数据</span>
          <span class="value">{{ formatBytes(repo.info.live_bytes) }}</span>
        </div>
        <div class="stat-card">
          <span class="label">孤儿块</span>
          <span class="value">{{ formatBytes(repo.info.orphan_bytes) }}</span>
        </div>
        <div class="stat-card">
          <span class="label">放大率</span>
          <span class="value">{{ repo.info.ampl_ratio.toFixed(2) }}×</span>
        </div>
        <div class="stat-card">
          <span class="label">唯一块</span>
          <span class="value">{{ repo.info.unique_chunks }}</span>
        </div>
        <div class="stat-card">
          <span class="label">墓碑块</span>
          <span class="value">{{ repo.info.tombstoned_chunks }}</span>
        </div>
      </div>
    </section>
    <EmptyState
      v-else
      title="请先打开仓库"
      action-label="前往仓库页"
      @action="ui.setActivity('repo')"
    />

    <section class="panel-card">
      <h2>物理压缩 (Compact)</h2>
      <el-switch v-model="compactDryRun" active-text="Dry run" />
      <FieldTip content="合并 pack 文件，降低放大率；先模拟再正式执行。" />
      <p v-if="lastCompactSummary" class="summary-line">{{ lastCompactSummary }}</p>
      <el-button
        type="primary"
        class="action-btn"
        :loading="busy"
        :disabled="!repo.isOpen"
        @click="runCompact"
      >
        运行 Compact
      </el-button>
    </section>

    <section class="panel-card">
      <h2>孤儿块 GC</h2>
      <el-switch v-model="gcDryRun" active-text="Dry run" />
      <FieldTip content="删除 manifest 不再引用的 chunk；Dry run 可预览回收量。" />
      <p v-if="lastGcSummary" class="summary-line">{{ lastGcSummary }}</p>
      <el-button
        type="primary"
        class="action-btn"
        :loading="busy"
        :disabled="!repo.isOpen"
        @click="runGc"
      >
        运行 GC
      </el-button>
    </section>
  </div>
</template>

<style scoped>
.activity-page {
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  overflow: auto;
  height: 100%;
}
.head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}
.head h2,
.panel-card h2 {
  margin: 0;
  font-size: 15px;
}
.head-actions {
  display: flex;
  gap: 8px;
  align-items: center;
}
.cards {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
  gap: 10px;
}
.stat-card {
  padding: 12px;
  border-radius: 8px;
  background: var(--hover-bg);
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.stat-card .label {
  font-size: 11px;
  color: var(--text-soft);
}
.stat-card .value {
  font-size: 16px;
  color: var(--text-main);
  font-weight: 600;
}
.summary-line {
  margin: 10px 0 0;
  font-size: 12px;
  color: var(--text-soft);
}
.action-btn {
  margin-top: 12px;
}
</style>
