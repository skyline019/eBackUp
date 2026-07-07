<script setup lang="ts">
import { onMounted, ref } from "vue";
import { pruneSnapshots, type SnapshotDto } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { confirmDestructive } from "@/utils/confirmDestructive";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";

const repo = useRepoStore();
const ui = useUiStore();

const retentionTiers = ref("1d:7,1w:4,1m:6");
const retainMin = ref(3);
const dryRun = ref(true);
const busy = ref(false);
const lastPruneSummary = ref("");

const TIER_PRESETS = [
  { label: "7 日保留", value: "1d:7" },
  { label: "标准 GFS", value: "1d:7,1w:4,1m:6" },
  { label: "轻量（3+2+2）", value: "1d:3,1w:2,1m:2" },
];

onMounted(async () => {
  if (repo.isOpen) await repo.refreshSnapshots();
});

async function refresh() {
  try {
    await repo.refreshSnapshots();
    ui.pushLog(`快照列表已刷新 (${repo.snapshots.length})`, "meta");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e)), "error");
  }
}

function applyPreset(value: string) {
  retentionTiers.value = value;
}

function restoreFromRow(txnId: number) {
  ui.goRestoreWithTxn(txnId);
}

function onSnapRowClick(row: SnapshotDto) {
  restoreFromRow(row.txn_id);
}

async function runPrune() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  if (!dryRun.value) {
    const ok = await confirmDestructive(
      "执行快照 Prune",
      `将按策略「${retentionTiers.value}」删除过期快照（最少保留 ${retainMin.value} 份）。此操作不可撤销。`
    );
    if (!ok) return;
  }
  busy.value = true;
  try {
    const res = await pruneSnapshots(retentionTiers.value, retainMin.value, dryRun.value);
    ui.setTaskResult(res);
    const kept = Number(res.kept_count ?? 0);
    const pruned = Number(res.pruned_count ?? 0);
    lastPruneSummary.value = `保留 ${kept}，删除 ${pruned}`;
    ui.pushLog(`Prune ${dryRun.value ? "(dry-run) " : ""}${lastPruneSummary.value}`, "success");
    if (!dryRun.value) await repo.refreshSnapshots();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "Prune 失败"), "error");
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <div class="activity-page">
    <section class="panel-card">
      <div class="head">
        <h2>快照列表</h2>
        <div class="head-actions">
          <el-button link type="primary" @click="ui.openHelp('activity')">帮助</el-button>
          <el-button size="small" :disabled="!repo.isOpen" @click="refresh">刷新</el-button>
        </div>
      </div>
      <el-table
        v-if="repo.snapshots.length"
        :data="repo.snapshots"
        size="small"
        stripe
        highlight-current-row
        class="snap-table"
        @row-click="onSnapRowClick"
      >
        <el-table-column prop="txn_id" label="Txn" width="80" />
        <el-table-column label="时间">
          <template #default="{ row }">
            {{ new Date(row.created_at_unix * 1000).toLocaleString() }}
          </template>
        </el-table-column>
        <el-table-column prop="file_count" label="文件数" width="90" />
        <el-table-column prop="manifest_crc32" label="CRC32" width="100" />
        <el-table-column label="" width="72">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click.stop="restoreFromRow(row.txn_id)">
              恢复
            </el-button>
          </template>
        </el-table-column>
      </el-table>
      <p v-else class="muted">尚无快照或未打开仓库 — 请先完成一次备份</p>
      <p v-if="repo.snapshots.length" class="table-hint">点击行或「恢复」跳转到恢复页并预选 Txn</p>
    </section>

    <section class="panel-card">
      <h2>保留策略 (GFS)</h2>
      <el-form label-width="120px">
        <el-form-item label="预设">
          <el-button
            v-for="p in TIER_PRESETS"
            :key="p.value"
            size="small"
            @click="applyPreset(p.value)"
          >
            {{ p.label }}
          </el-button>
        </el-form-item>
        <el-form-item label="Tiers">
          <el-input v-model="retentionTiers" placeholder="1d:7,1w:4,1m:6" />
          <FieldTip content="格式：1d:7 表示每日 7 份，1w:4 每周 4 份，1m:6 每月 6 份。" />
        </el-form-item>
        <el-form-item label="最少保留">
          <el-input-number v-model="retainMin" :min="1" :max="100" />
        </el-form-item>
        <el-form-item label="Dry run">
          <el-switch v-model="dryRun" />
          <FieldTip content="开启时仅模拟，不删除快照；确认后再关闭并执行。" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="busy" :disabled="!repo.isOpen" @click="runPrune">
            执行 Prune
          </el-button>
        </el-form-item>
        <p v-if="lastPruneSummary" class="muted">上次结果：{{ lastPruneSummary }}</p>
      </el-form>
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
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}
.head h2 {
  margin: 0;
  font-size: 15px;
}
.head-actions {
  display: flex;
  gap: 8px;
  align-items: center;
}
.panel-card h2 {
  margin: 0 0 12px;
  font-size: 15px;
}
.muted {
  color: var(--text-soft);
  font-size: 13px;
}
.table-hint {
  margin: 8px 0 0;
  font-size: 11px;
  color: var(--text-soft);
}
.snap-table {
  cursor: pointer;
}
</style>
