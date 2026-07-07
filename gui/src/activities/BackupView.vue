<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import {
  runBackup,
  pickDirectory,
  pickFile,
  loadFilterFile,
  setPassword,
  pathExists,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";

const repo = useRepoStore();
const ui = useUiStore();

const sourcePath = ref("");
const incremental = ref(true);
const useLz4 = ref(true);
const usePipeline = ref(true);
const useEncrypt = ref(false);
const password = ref("");
const filterPath = ref("");
const running = ref(false);

const FLAG_LZ4 = 0x0001;
const FLAG_PIPELINE = 0x0002;
const FLAG_ENCRYPT = 0x0008;
const FLAG_COMPRESS_AUTO = 0x0020;

const isFirstBackup = computed(
  () => repo.isOpen && repo.snapshots.length === 0 && (repo.info?.manifest_bytes ?? 0) === 0
);

function backupErrorMessage(raw: unknown): string {
  const msg = raw instanceof Error ? raw.message : String(raw);
  if (msg.includes("source path not found")) {
    return "源目录不存在，请点「浏览」重新选择有效文件夹";
  }
  if (msg.includes("prior manifest not found")) {
    return "尚无历史备份，首次请取消「增量备份」或改用全量模式";
  }
  return msg.replace(/^run_backup:\s*/i, "");
}

onMounted(async () => {
  repo.loadLocal();
  if (repo.lastSourcePath) {
    try {
      if (await pathExists(repo.lastSourcePath)) {
        sourcePath.value = repo.lastSourcePath;
      }
    } catch {
      /* ignore */
    }
  }
  syncIncrementalDefault();
});

watch(
  () => [repo.isOpen, repo.snapshots.length, repo.info?.manifest_bytes] as const,
  () => syncIncrementalDefault()
);

function syncIncrementalDefault() {
  if (isFirstBackup.value) incremental.value = false;
}

function buildFlags() {
  let f = FLAG_COMPRESS_AUTO;
  if (useLz4.value) f |= FLAG_LZ4;
  if (usePipeline.value) f |= FLAG_PIPELINE;
  if (useEncrypt.value) f |= FLAG_ENCRYPT;
  return f;
}

async function browseSource() {
  const p = await pickDirectory();
  if (p) sourcePath.value = p;
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
  const src = sourcePath.value.trim();
  if (!src) {
    ui.pushLog("请选择源目录", "error");
    return;
  }
  try {
    if (!(await pathExists(src))) {
      ui.pushLog("源目录不存在，请重新选择", "error");
      return;
    }
  } catch (e) {
    ui.pushLog(backupErrorMessage(e), "error");
    return;
  }
  if (incremental.value && isFirstBackup.value) {
    ui.pushLog("首次备份已自动使用全量模式", "meta");
    incremental.value = false;
  }
  running.value = true;
  ui.pushLog(`备份: ${src}`, "cmd");
  try {
    if (useEncrypt.value && password.value) {
      await setPassword(password.value);
    }
    if (filterPath.value.trim()) {
      await loadFilterFile(filterPath.value.trim());
    }
    const stats = await runBackup(src, incremental.value, buildFlags());
    repo.setLastSource(src);
    ui.setTaskResult(stats);
    ui.lastResultJson = JSON.stringify(stats, null, 2);
    ui.outputTab = "results";
    await repo.refreshInfo();
    await repo.refreshSnapshots();
    ui.pushLog(
      `备份完成 — 文件 ${stats.files_processed}，写入块 ${stats.chunks_written}，复用 ${stats.chunks_reused}`,
      "success"
    );
  } catch (e) {
    ui.pushLog(backupErrorMessage(e), "error");
  } finally {
    running.value = false;
  }
}
</script>

<template>
  <div class="activity-page backup-view">
    <section class="panel-card">
      <h2>运行备份</h2>
      <el-alert v-if="!repo.isOpen" type="warning" show-icon :closable="false" title="请先在「仓库」页打开备份仓库" />
      <template v-else>
        <el-alert
          v-if="isFirstBackup"
          type="info"
          show-icon
          :closable="false"
          class="first-backup-hint"
          title="空仓库首次备份：请选择存在的源目录，将自动使用全量模式"
        />
        <el-form label-width="110px" @submit.prevent="run">
          <el-form-item label="源目录">
            <div class="row">
              <el-input v-model="sourcePath" placeholder="要备份的文件夹" />
              <el-button @click="browseSource">浏览</el-button>
            </div>
          </el-form-item>
          <el-form-item label="模式">
            <el-checkbox v-model="incremental" :disabled="isFirstBackup">增量备份</el-checkbox>
          </el-form-item>
          <el-form-item label="选项">
            <el-checkbox v-model="useLz4">LZ4</el-checkbox>
            <el-checkbox v-model="usePipeline">Pipeline</el-checkbox>
            <el-checkbox v-model="useEncrypt">加密</el-checkbox>
          </el-form-item>
          <el-form-item v-if="useEncrypt" label="密码">
            <el-input v-model="password" type="password" show-password />
          </el-form-item>
          <el-form-item label="过滤器文件">
            <div class="row">
              <el-input v-model="filterPath" placeholder="可选 .filter 文件" />
              <el-button @click="browseFilter">浏览</el-button>
            </div>
          </el-form-item>
          <el-form-item>
            <el-button type="primary" :loading="running" @click="run">运行备份</el-button>
          </el-form-item>
        </el-form>
      </template>
    </section>
  </div>
</template>

<style scoped>
.activity-page {
  padding: 16px 20px;
  overflow: auto;
  height: 100%;
}
.first-backup-hint {
  margin-bottom: 12px;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
</style>
