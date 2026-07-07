<script setup lang="ts">
import { onMounted, ref, watch } from "vue";
import {
  runRestore,
  pickDirectory,
  pickFile,
  setPassword,
  loadFilterFile,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { enrichError, formatRestoreError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";

const repo = useRepoStore();
const ui = useUiStore();

const destPath = ref("");
const txnId = ref<number | undefined>(undefined);
const password = ref("");
const filterPath = ref("");
const skipContentVerify = ref(false);
const running = ref(false);

const FLAG_SKIP_CONTENT_VERIFY = 0x0001;

onMounted(() => {
  applyPrefill();
});

watch(
  () => ui.restoreTxnPrefill,
  () => applyPrefill()
);

function applyPrefill() {
  const pre = ui.consumeRestoreTxn();
  if (pre != null && pre > 0) txnId.value = pre;
}

async function browseDest() {
  const p = await pickDirectory();
  if (p) destPath.value = p;
}

async function browseFilter() {
  const p = await pickFile();
  if (p) filterPath.value = p;
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
    if (password.value) await setPassword(password.value);
    if (filterPath.value.trim()) await loadFilterFile(filterPath.value.trim());
    let flags = 0;
    if (skipContentVerify.value) flags |= FLAG_SKIP_CONTENT_VERIFY;
    const res = await runRestore(destPath.value.trim(), txnId.value || undefined, flags);
    ui.setTaskResult(res);
    ui.lastResultJson = JSON.stringify(res, null, 2);
    ui.outputTab = "results";
    ui.pushLog("恢复完成", "success");
  } catch (e) {
    ui.pushLog(formatRestoreError(await enrichError(e)), "error");
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
      <div class="head-row">
        <h2>还原文件</h2>
        <el-button link type="primary" @click="ui.openHelp('activity')">本页帮助</el-button>
      </div>
      <EmptyState
        v-if="!repo.isOpen"
        title="请先打开仓库"
        action-label="前往仓库页"
        @action="ui.setActivity('repo')"
      />
      <el-form v-else label-width="120px" @submit.prevent="run">
        <el-form-item label="目标目录">
          <div class="row">
            <el-input v-model="destPath" placeholder="还原输出位置" />
            <el-button @click="browseDest">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="快照 Txn">
          <el-input-number v-model="txnId" :min="0" placeholder="留空=最新" />
          <span class="hint">留空表示最新快照</span>
          <FieldTip content="时间旅行恢复：指定历史 txn_id；侧栏/快照页可一键填入。" />
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
        <el-form-item label="密码">
          <el-input
            v-model="password"
            type="password"
            show-password
            placeholder="加密备份时必填"
          />
        </el-form-item>
        <el-form-item label="过滤器">
          <div class="row">
            <el-input v-model="filterPath" placeholder="可选，与备份相同规则" />
            <el-button @click="browseFilter">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="高级">
          <el-checkbox v-model="skipContentVerify">跳过内容校验</el-checkbox>
          <FieldTip content="仅恢复元数据或调试时使用；默认应保持关闭。" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="running" @click="run">开始恢复</el-button>
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
.head-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}
.head-row h2 {
  margin: 0;
  font-size: 15px;
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
