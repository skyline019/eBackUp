<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useRepoStore } from "@/stores/repoStore";
import { useJobStore } from "@/stores/jobStore";
import { useUiStore } from "@/stores/uiStore";
import { pickDirectory } from "@/api/ebbackup";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import {
  rpoSummaryData,
  rpoLoading,
  staleAlertVisible,
  syncStatusData,
  staleSyncAlertVisible,
  refreshRpoSummary,
  refreshSyncStatus,
} from "@/composables/useBackupAlerts";
import { formatTransportLabel, staleSyncMessage } from "@/utils/syncLabels";
import FieldTip from "@/components/FieldTip.vue";

const repo = useRepoStore();
const jobs = useJobStore();
const ui = useUiStore();

const parentDir = ref("E:\\data");
const repoName = ref("mybackup");
const openPath = ref("");
const initLegacy = ref(false);

const FLAG_INIT_LEGACY = 0x0200;

onMounted(() => {
  repo.loadLocal();
  if (repo.recent[0]) openPath.value = repo.recent[0];
  if (repo.isOpen) void jobs.refreshJobs();
});

async function browseParent() {
  const p = await pickDirectory();
  if (p) parentDir.value = p;
}

async function browseOpen() {
  const p = await pickDirectory();
  if (p) openPath.value = p;
}

async function createRepo() {
  try {
    const flags = initLegacy.value ? FLAG_INIT_LEGACY : 0;
    await repo.createRepo(parentDir.value, repoName.value, flags);
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "创建失败"), "error");
  }
}

async function openExisting() {
  if (!openPath.value.trim()) {
    ui.pushLog("请输入仓库路径", "error");
    return;
  }
  try {
    await repo.open(openPath.value.trim());
  } catch (e) {
    ui.pushLog(formatGenericError(e), "error");
  }
}

async function openRecent(path: string) {
  try {
    await repo.open(path);
  } catch (e) {
    ui.pushLog(formatGenericError(e), "error");
  }
}

async function closeRepo() {
  try {
    await repo.close();
  } catch (e) {
    ui.pushLog(String(e), "error");
  }
}

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${(n / 1024 / 1024).toFixed(1)} MiB`;
}

function formatRpoTime(unix: number) {
  if (!unix) return "—";
  return new Date(unix * 1000).toLocaleString();
}

const rpoCard = computed(() => rpoSummaryData.value);
const syncCard = computed(() => syncStatusData.value);
const syncCardTransport = computed(() =>
  syncCard.value
    ? formatTransportLabel(syncCard.value.transport, syncCard.value.remote_type)
    : "—"
);
const syncStaleTitle = computed(() =>
  syncCard.value && staleSyncAlertVisible.value ? staleSyncMessage(syncCard.value) : ""
);
</script>

<template>
  <div class="activity-page repo-home">
    <section class="panel-card">
      <div class="head-row">
        <h2>新建仓库</h2>
        <el-button link type="primary" @click="ui.openHelp('quickstart')">演示流程</el-button>
      </div>
      <el-form label-width="110px" @submit.prevent="createRepo">
        <el-form-item label="父目录">
          <div class="row">
            <el-input v-model="parentDir" />
            <el-button @click="browseParent">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="名称">
          <el-input v-model="repoName" placeholder="mybackup" />
        </el-form-item>
        <el-form-item label="兼容">
          <el-checkbox v-model="initLegacy">v0.3 旧版布局</el-checkbox>
          <FieldTip content="仅在与旧仓库格式对接时开启；新课程项目保持关闭。" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="repo.busy" @click="createRepo">
            创建并打开
          </el-button>
        </el-form-item>
      </el-form>
    </section>

    <section class="panel-card">
      <h2>打开已有仓库</h2>
      <el-form label-width="110px" @submit.prevent="openExisting">
        <el-form-item label="路径">
          <div class="row">
            <el-input v-model="openPath" />
            <el-button @click="browseOpen">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="repo.busy" @click="openExisting">打开</el-button>
          <el-button v-if="repo.isOpen" @click="closeRepo">关闭</el-button>
        </el-form-item>
      </el-form>
    </section>

    <section v-if="repo.recent.length" class="panel-card">
      <h2>最近仓库</h2>
      <ul class="recent-list">
        <li v-for="p in repo.recent" :key="p">
          <button type="button" class="recent-btn" @click="openRecent(p)">{{ p }}</button>
        </li>
      </ul>
    </section>

    <section v-if="repo.info" class="panel-card summary">
      <h2>仓库摘要</h2>
      <el-alert
        v-if="staleAlertVisible && repo.isOpen"
        type="warning"
        show-icon
        :closable="false"
        class="stale-alert"
        :title="
          rpoCard?.snapshot_count === 0
            ? '尚无成功备份'
            : `距上次成功备份 ${rpoCard?.days_since_last_success?.toFixed(1) ?? '?'} 天`
        "
      />
      <el-alert
        v-if="staleSyncAlertVisible && repo.isOpen"
        type="warning"
        show-icon
        :closable="false"
        class="stale-alert"
        :title="syncStaleTitle || '同步滞后'"
      />
      <section v-if="syncCard" class="rpo-card sync-card">
        <div class="head-row">
          <h3>同步摘要</h3>
          <el-button link size="small" @click="refreshSyncStatus">刷新</el-button>
        </div>
        <dl class="rpo-dl">
          <dt>模式</dt>
          <dd>{{ syncCard.sync_mode_label || syncCardTransport }}</dd>
          <dt>已同步 txn</dt>
          <dd>{{ syncCard.synced_txn }} / {{ syncCard.latest_txn }}</dd>
          <dt>传输</dt>
          <dd>{{ syncCardTransport }}</dd>
          <dt>上次同步</dt>
          <dd>{{ formatRpoTime(syncCard.last_success_unix) }}</dd>
        </dl>
        <el-button type="primary" link @click="ui.setActivity('sync')">前往同步 →</el-button>
      </section>
      <section v-if="rpoCard" class="rpo-card">
        <div class="head-row">
          <h3>RPO 合规摘要</h3>
          <el-button link size="small" :loading="rpoLoading" @click="refreshRpoSummary">刷新</el-button>
        </div>
        <dl class="rpo-dl">
          <dt>上次成功</dt>
          <dd>{{ formatRpoTime(rpoCard.last_success_unix) }}</dd>
          <dt>距今天数</dt>
          <dd>{{ rpoCard.days_since_last_success >= 0 ? rpoCard.days_since_last_success.toFixed(1) : "—" }}</dd>
          <dt>快照数</dt>
          <dd>{{ rpoCard.snapshot_count }}</dd>
          <dt>WORM 保护</dt>
          <dd>{{ rpoCard.worm_protected_count }}</dd>
        </dl>
        <div class="rpo-links">
          <el-button type="primary" link @click="ui.setActivity('snapshots')">查看快照 →</el-button>
          <el-button type="primary" link @click="ui.setActivity('backup')">前往备份 →</el-button>
        </div>
      </section>
      <dl>
        <dt>路径</dt><dd>{{ repo.path }}</dd>
        <dt>ABI</dt><dd>v{{ repo.info.abi_version }}</dd>
        <dt>物理占用</dt><dd>{{ formatBytes(repo.info.physical_bytes) }}</dd>
        <dt>有效数据</dt><dd>{{ formatBytes(repo.info.live_bytes) }}</dd>
        <dt>快照数</dt><dd>{{ repo.snapshots.length }}</dd>
        <dt>备份作业</dt><dd>{{ jobs.jobs.length }} 个</dd>
      </dl>
      <el-button type="primary" link @click="ui.setActivity('backup')">前往备份 / 作业管理 →</el-button>
    </section>
  </div>
</template>

<style scoped>
.activity-page.repo-home {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 16px 20px;
  overflow: auto;
  height: 100%;
}
.stale-alert {
  margin-bottom: 12px;
}
.rpo-card {
  margin-bottom: 16px;
  padding: 12px 14px;
  border-radius: 8px;
  background: color-mix(in srgb, var(--accent) 8%, transparent);
  border: 1px solid color-mix(in srgb, var(--accent) 22%, transparent);
}
.rpo-card h3 {
  margin: 0;
  font-size: 14px;
}
.rpo-dl {
  display: grid;
  grid-template-columns: 120px 1fr;
  gap: 6px 12px;
  margin: 10px 0;
  font-size: 13px;
}
.rpo-links {
  display: flex;
  gap: 12px;
}
.head-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}
.head-row h2,
.panel-card h2 {
  margin: 0;
  font-size: 15px;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
.recent-list {
  list-style: none;
  margin: 0;
  padding: 0;
}
.recent-btn {
  width: 100%;
  text-align: left;
  padding: 8px 10px;
  margin-bottom: 4px;
  border: none;
  border-radius: 6px;
  background: var(--hover-bg);
  color: var(--text-regular);
  cursor: pointer;
  font-size: 12px;
  word-break: break-all;
}
.recent-btn:hover {
  background: color-mix(in srgb, var(--accent) 12%, var(--hover-bg));
}
.summary dl {
  display: grid;
  grid-template-columns: 120px 1fr;
  gap: 8px;
  font-size: 13px;
  margin-bottom: 12px;
}
.summary dt {
  color: var(--text-soft);
}
.summary dd {
  margin: 0;
  color: var(--text-main);
  word-break: break-all;
}
</style>
