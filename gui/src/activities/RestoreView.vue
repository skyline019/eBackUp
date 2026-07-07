<script setup lang="ts">
import { ref } from "vue";
import { runRestore, pickDirectory } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";

const repo = useRepoStore();
const ui = useUiStore();

const destPath = ref("");
const txnId = ref<number | undefined>(undefined);
const running = ref(false);

async function browseDest() {
  const p = await pickDirectory();
  if (p) destPath.value = p;
}

async function run() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  if (!destPath.value.trim()) {
    ui.pushLog("请选择目标目录", "error");
    return;
  }
  running.value = true;
  ui.pushLog(`恢复 → ${destPath.value}`, "cmd");
  try {
    const res = await runRestore(destPath.value.trim(), txnId.value || undefined, 0);
    ui.setTaskResult(res);
    ui.lastResultJson = JSON.stringify(res, null, 2);
    ui.outputTab = "results";
    ui.pushLog("恢复完成", "success");
  } catch (e) {
    ui.pushLog(String(e), "error");
  } finally {
    running.value = false;
  }
}

function pickSnapshot(txn: number) {
  txnId.value = txn;
}
</script>

<template>
  <div class="activity-page">
    <section class="panel-card">
      <h2>还原文件</h2>
      <el-form label-width="110px" @submit.prevent="run">
        <el-form-item label="目标目录">
          <div class="row">
            <el-input v-model="destPath" />
            <el-button @click="browseDest">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="快照 Txn">
          <el-input-number v-model="txnId" :min="0" placeholder="留空=最新" />
          <span class="hint">留空表示最新快照</span>
        </el-form-item>
        <el-form-item v-if="repo.snapshots.length" label="快速选择">
          <div class="snap-pills">
            <el-button
              v-for="s in repo.snapshots"
              :key="s.txn_id"
              size="small"
              :type="txnId === s.txn_id ? 'primary' : 'default'"
              @click="pickSnapshot(s.txn_id)"
            >
              #{{ s.txn_id }}
            </el-button>
          </div>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="running" :disabled="!repo.isOpen" @click="run">
            开始恢复
          </el-button>
        </el-form-item>
      </el-form>
    </section>
  </div>
</template>

<style scoped>
.activity-page {
  padding: 16px 20px;
  overflow: auto;
  height: 100%;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
.hint {
  margin-left: 8px;
  font-size: 12px;
  color: var(--text-soft);
}
.snap-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
</style>
