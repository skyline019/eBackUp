<script setup lang="ts">
import { onMounted, ref } from "vue";
import { pruneSnapshots } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";

const repo = useRepoStore();
const ui = useUiStore();

const retentionTiers = ref("1d:7,1w:4,1m:6");
const retainMin = ref(3);
const dryRun = ref(true);
const busy = ref(false);

onMounted(async () => {
  if (repo.isOpen) await repo.refreshSnapshots();
});

async function refresh() {
  try {
    await repo.refreshSnapshots();
    ui.pushLog(`快照列表已刷新 (${repo.snapshots.length})`, "meta");
  } catch (e) {
    ui.pushLog(String(e), "error");
  }
}

async function runPrune() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  busy.value = true;
  try {
    const res = await pruneSnapshots(retentionTiers.value, retainMin.value, dryRun.value);
    ui.setTaskResult(res);
    ui.pushLog(
      `Prune ${dryRun.value ? "(dry-run) " : ""}保留 ${res.kept_count} / 删除 ${res.pruned_count}`,
      "success"
    );
    if (!dryRun.value) await repo.refreshSnapshots();
  } catch (e) {
    ui.pushLog(String(e), "error");
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
        <el-button size="small" :disabled="!repo.isOpen" @click="refresh">刷新</el-button>
      </div>
      <el-table v-if="repo.snapshots.length" :data="repo.snapshots" size="small" stripe>
        <el-table-column prop="txn_id" label="Txn" width="80" />
        <el-table-column label="时间">
          <template #default="{ row }">
            {{ new Date(row.created_at_unix * 1000).toLocaleString() }}
          </template>
        </el-table-column>
        <el-table-column prop="file_count" label="文件数" width="90" />
        <el-table-column prop="manifest_crc32" label="CRC32" width="100" />
      </el-table>
      <p v-else class="muted">尚无快照或未打开仓库</p>
    </section>

    <section class="panel-card">
      <h2>保留策略 (GFS)</h2>
      <el-form label-width="120px">
        <el-form-item label="Tiers">
          <el-input v-model="retentionTiers" placeholder="1d:7,1w:4,1m:6" />
        </el-form-item>
        <el-form-item label="最少保留">
          <el-input-number v-model="retainMin" :min="1" :max="100" />
        </el-form-item>
        <el-form-item label="Dry run">
          <el-switch v-model="dryRun" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="busy" :disabled="!repo.isOpen" @click="runPrune">
            执行 Prune
          </el-button>
        </el-form-item>
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
.muted {
  color: var(--text-soft);
  font-size: 13px;
}
</style>
