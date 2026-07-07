<script setup lang="ts">
import { onMounted, ref } from "vue";
import { compactRepo, gcOrphans } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";

const repo = useRepoStore();
const ui = useUiStore();

const compactDryRun = ref(true);
const gcDryRun = ref(true);
const busy = ref(false);

onMounted(async () => {
  if (repo.isOpen) await repo.refreshInfo();
});

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MiB`;
  return `${(n / 1024 / 1024 / 1024).toFixed(2)} GiB`;
}

async function refreshStats() {
  try {
    await repo.refreshInfo();
    ui.pushLog("统计已刷新", "meta");
  } catch (e) {
    ui.pushLog(String(e), "error");
  }
}

async function runCompact() {
  if (!repo.isOpen) return;
  busy.value = true;
  try {
    const res = await compactRepo(compactDryRun.value);
    ui.setTaskResult(res);
    ui.pushLog(`Compact ${compactDryRun.value ? "(dry-run)" : ""} 完成`, "success");
    if (!compactDryRun.value) await repo.refreshInfo();
  } catch (e) {
    ui.pushLog(String(e), "error");
  } finally {
    busy.value = false;
  }
}

async function runGc() {
  if (!repo.isOpen) return;
  busy.value = true;
  try {
    const res = await gcOrphans(gcDryRun.value);
    ui.setTaskResult(res);
    ui.pushLog(`GC orphans ${gcDryRun.value ? "(dry-run)" : ""} 完成`, "success");
    if (!gcDryRun.value) await repo.refreshInfo();
  } catch (e) {
    ui.pushLog(String(e), "error");
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
        <el-button size="small" @click="refreshStats">刷新</el-button>
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

    <section class="panel-card">
      <h2>物理压缩 (Compact)</h2>
      <el-switch v-model="compactDryRun" active-text="Dry run" />
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
  margin: 0 0 12px;
  font-size: 15px;
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
.action-btn {
  margin-top: 12px;
}
</style>
