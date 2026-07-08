<script setup lang="ts">
import { computed } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { useRepoStore } from "@/stores/repoStore";
import { useJobStore } from "@/stores/jobStore";
import { enrichError, formatBackupError } from "@/utils/errorMessages";
import { RefreshRight } from "@element-plus/icons-vue";

const ui = useUiStore();
const repo = useRepoStore();
const jobs = useJobStore();

const quickJobs = computed(() => jobs.jobs.slice(0, 5));

const title = computed(() => {
  switch (ui.activity) {
    case "repo":
      return "最近仓库";
    case "backup":
      return "备份上下文";
    case "snapshots":
      return "快照";
    case "browse":
      return "备份内容";
    case "restore":
      return "恢复";
    case "verify":
      return "验证";
    case "maintenance":
      return "维护";
    default:
      return "上下文";
  }
});

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MiB`;
  return `${(n / 1024 / 1024 / 1024).toFixed(2)} GiB`;
}

function openRecent(path: string) {
  void repo.open(path);
}

function goRestore(txnId: number) {
  ui.goRestoreWithTxn(txnId);
}

function goBrowse(txnId: number) {
  ui.goBrowseWithTxn(txnId);
}

function goBackupJobs() {
  ui.setActivity("backup");
}

async function quickRunJob(jobId: string) {
  if (!repo.isOpen) return;
  ui.setActivity("backup");
  ui.pushLog(`运行作业: ${jobId}`, "cmd");
  try {
    const stats = await jobs.runJob(jobId, true, 0x0020 | 0x0001 | 0x0002);
    ui.setTaskResult(stats);
    ui.pushLog(`作业 ${jobId} 完成 — 文件 ${stats.files_processed}`, "success");
    await repo.refreshSnapshots();
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  }
}
</script>

<template>
  <aside class="context-sidebar sidebar" :style="{ width: 'var(--sidebar-width)' }">
    <header class="ctx-header">
      <h3>{{ title }}</h3>
      <el-button
        v-if="repo.isOpen"
        size="small"
        :icon="RefreshRight"
        circle
        title="刷新"
        @click="repo.refreshSnapshots()"
      />
    </header>
    <div class="ctx-body">
      <template v-if="ui.activity === 'repo'">
        <ul class="recent-list">
          <li v-for="p in repo.recent" :key="p">
            <button type="button" class="recent-btn" @click="openRecent(p)">{{ p }}</button>
          </li>
        </ul>
        <p v-if="!repo.recent.length" class="muted">暂无最近仓库</p>
        <el-button link type="primary" @click="ui.openHelp('quickstart')">查看演示流程</el-button>
      </template>

      <template v-else-if="ui.activity === 'backup' && repo.isOpen">
        <dl class="stats-dl">
          <dt>作业数</dt>
          <dd>{{ jobs.jobs.length }}</dd>
          <dt>快照数</dt>
          <dd>{{ repo.snapshots.length }}</dd>
        </dl>
        <ul v-if="quickJobs.length" class="snap-list">
          <li v-for="j in quickJobs" :key="j.id" class="snap-item">
            <strong>{{ j.name }}</strong>
            <span class="muted mono">{{ j.id }}</span>
            <el-button link type="primary" size="small" @click="quickRunJob(j.id)">运行</el-button>
          </li>
        </ul>
        <p v-else class="muted">尚无保存的作业</p>
        <el-button link type="primary" @click="goBackupJobs">管理作业 →</el-button>
      </template>

      <template v-else-if="ui.activity === 'snapshots' && repo.isOpen">
        <ul class="snap-list">
          <li v-for="s in repo.snapshots" :key="s.txn_id" class="snap-item clickable" @click="goRestore(s.txn_id)">
            <strong>#{{ s.txn_id }}</strong>
            <span>{{ new Date(s.created_at_unix * 1000).toLocaleString() }}</span>
            <span class="muted">{{ s.file_count }} 文件 · 点击恢复</span>
          </li>
        </ul>
        <p v-if="!repo.snapshots.length" class="muted">尚无快照</p>
      </template>

      <template v-else-if="ui.activity === 'browse' && repo.isOpen">
        <p class="muted">选择 Txn 后刷新，可搜索路径查看 manifest 中的文件与目录。</p>
        <ul v-if="repo.snapshots.length" class="snap-list">
          <li
            v-for="s in repo.snapshots.slice(0, 8)"
            :key="s.txn_id"
            class="snap-item clickable"
            @click="goBrowse(s.txn_id)"
          >
            <strong>#{{ s.txn_id }}</strong>
            <span class="muted">{{ s.file_count }} 文件</span>
          </li>
        </ul>
      </template>

      <template v-else-if="ui.activity === 'restore' && repo.isOpen">
        <p class="muted">在右侧选择目标目录与 Txn；加密仓库需填密码。</p>
        <ul v-if="repo.snapshots.length" class="snap-list">
          <li
            v-for="s in repo.snapshots.slice(0, 8)"
            :key="s.txn_id"
            class="snap-item clickable"
            @click="goRestore(s.txn_id)"
          >
            <strong>#{{ s.txn_id }}</strong>
            <span class="muted">{{ s.file_count }} 文件</span>
          </li>
        </ul>
      </template>

      <template v-else-if="ui.activity === 'verify'">
        <p class="muted">默认关闭「强制锚点」即可答辩演示；签名验证在验证页填写密钥。</p>
      </template>

      <template v-else-if="repo.info">
        <dl class="stats-dl">
          <dt>物理占用</dt>
          <dd>{{ formatBytes(repo.info.physical_bytes) }}</dd>
          <dt>有效数据</dt>
          <dd>{{ formatBytes(repo.info.live_bytes) }}</dd>
          <dt>孤儿块</dt>
          <dd>{{ formatBytes(repo.info.orphan_bytes) }}</dd>
          <dt>放大率</dt>
          <dd>{{ repo.info.ampl_ratio.toFixed(2) }}×</dd>
        </dl>
      </template>

      <p v-else class="muted">请先打开仓库</p>
    </div>
  </aside>
</template>

<style scoped>
.ctx-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 12px;
  border-bottom: 1px solid var(--shell-line);
}
.ctx-header h3 {
  margin: 0;
  font-size: 13px;
  color: var(--text-main);
}
.ctx-body {
  padding: 10px 12px;
  overflow: auto;
  flex: 1;
}
.recent-list,
.snap-list {
  list-style: none;
  margin: 0;
  padding: 0;
}
.recent-btn {
  width: 100%;
  text-align: left;
  padding: 6px 8px;
  border: none;
  border-radius: 6px;
  background: transparent;
  color: var(--text-regular);
  cursor: pointer;
  font-size: 12px;
  word-break: break-all;
}
.recent-btn:hover {
  background: var(--hover-bg);
}
.snap-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 8px 0;
  border-bottom: 1px solid var(--shell-line);
  font-size: 12px;
}
.snap-item.clickable {
  cursor: pointer;
  border-radius: 4px;
  padding: 8px 6px;
}
.snap-item.clickable:hover {
  background: var(--hover-bg);
}
.stats-dl {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 6px 12px;
  font-size: 12px;
}
.stats-dl dt {
  color: var(--text-soft);
}
.stats-dl dd {
  margin: 0;
  color: var(--text-main);
  word-break: break-all;
}
.stats-dl .mono {
  font-family: var(--font-mono);
  font-size: 11px;
}
.muted {
  color: var(--text-soft);
  font-size: 12px;
  line-height: 1.5;
}
</style>
