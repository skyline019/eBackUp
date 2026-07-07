<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
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
import { useTaskStore } from "@/stores/taskStore";
import { setActivityRunner } from "@/composables/useActivityRunners";
import { enrichError, formatBackupError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";
import BackupPipelineViz from "@/components/BackupPipelineViz.vue";
import { isTauriRuntime } from "@/utils/tauriRuntime";

const repo = useRepoStore();
const ui = useUiStore();
const task = useTaskStore();

const sourcePath = ref("");
const incremental = ref(true);
const useLz4 = ref(true);
const usePipeline = ref(true);
const useEncrypt = ref(false);
const useZstd = ref(false);
const balancedDurability = ref(false);
const password = ref("");
const filterPath = ref("");
const showAdvanced = ref(false);
const running = ref(false);

const FLAG_LZ4 = 0x0001;
const FLAG_PIPELINE = 0x0002;
const FLAG_ENCRYPT = 0x0008;
const FLAG_COMPRESS_AUTO = 0x0020;
const FLAG_COMPRESS_ZSTD = 0x0040;
const FLAG_BALANCED_DURABILITY = 0x0080;

const isFirstBackup = computed(
  () => repo.isOpen && repo.snapshots.length === 0 && (repo.info?.manifest_bytes ?? 0) === 0
);

const pipelineActive = computed(() => {
  if (!task.active || task.active.kind !== "backup") return undefined;
  const p = task.active.permille;
  if (p < 50) return "scan";
  if (p < 400) return "chunk";
  if (p < 550) return "encode";
  if (p < 960) return "store";
  return "commit";
});

const showPipeline = computed(
  () => repo.isOpen && (task.isRunning || task.lastFinished?.kind === "backup")
);

let unregRunner: (() => void) | null = null;

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
  unregRunner = setActivityRunner("backup-run", run);
});

onUnmounted(() => unregRunner?.());

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
  if (useZstd.value) f |= FLAG_COMPRESS_ZSTD;
  if (balancedDurability.value) f |= FLAG_BALANCED_DURABILITY;
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
    ui.pushLog(formatBackupError(e), "error");
    return;
  }
  if (incremental.value && isFirstBackup.value) {
    ui.pushLog("首次备份已自动使用全量模式", "meta");
    incremental.value = false;
  }
  running.value = true;
  if (!isTauriRuntime()) task.start("backup");
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
    const reusePct =
      stats.chunks_written + stats.chunks_reused > 0
        ? Math.round(
            (100 * stats.chunks_reused) / (stats.chunks_written + stats.chunks_reused)
          )
        : 0;
    ui.pushLog(
      `备份完成 — 文件 ${stats.files_processed}，写入块 ${stats.chunks_written}，复用 ${stats.chunks_reused}（${reusePct}%）`,
      "success"
    );
    if (!isTauriRuntime()) task.finish(true);
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
    if (!isTauriRuntime()) task.finish(false);
  } finally {
    running.value = false;
  }
}
</script>

<template>
  <div class="activity-page backup-view">
    <section class="panel-card">
      <div class="head-row">
        <h2>运行备份</h2>
        <el-button link type="primary" @click="ui.openHelp('activity')">本页帮助</el-button>
      </div>
      <el-alert v-if="!repo.isOpen" type="warning" show-icon :closable="false" title="请先在「仓库」页打开备份仓库" />
      <EmptyState
        v-if="!repo.isOpen"
        title="尚未打开仓库"
        hint="创建或打开 repo 后即可备份"
        action-label="前往仓库页"
        @action="ui.setActivity('repo')"
      />
      <template v-else>
        <section v-if="showPipeline" class="pipeline-section panel-card">
          <h3 class="pipeline-title">备份管线（实时）</h3>
          <BackupPipelineViz :active-id="pipelineActive" />
        </section>
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
            <FieldTip content="必须是本机已存在的文件夹路径；备份后记住为「上次源目录」。" />
          </el-form-item>
          <el-form-item label="模式">
            <el-checkbox v-model="incremental" :disabled="isFirstBackup">
              增量备份
            </el-checkbox>
            <FieldTip content="有历史 manifest 时仅备份变更；首次或无 manifest 时请用全量。" />
          </el-form-item>
          <el-form-item label="选项">
            <el-checkbox v-model="useLz4">LZ4</el-checkbox>
            <el-checkbox v-model="usePipeline">Pipeline</el-checkbox>
            <el-checkbox v-model="useEncrypt">加密</el-checkbox>
            <FieldTip content="LZ4/Pipeline 为推荐默认；加密使用 AES-GCM，恢复时需相同密码。" />
          </el-form-item>
          <el-form-item v-if="useEncrypt" label="密码">
            <el-input v-model="password" type="password" show-password placeholder="备份加密密码" />
          </el-form-item>
          <el-form-item label="过滤器">
            <div class="row">
              <el-input v-model="filterPath" placeholder="可选 .filter 文件" />
              <el-button @click="browseFilter">浏览</el-button>
            </div>
          </el-form-item>
          <el-form-item>
            <el-button link type="primary" @click="showAdvanced = !showAdvanced">
              {{ showAdvanced ? "收起高级选项" : "高级选项" }}
            </el-button>
          </el-form-item>
          <template v-if="showAdvanced">
            <el-form-item label="压缩">
              <el-checkbox v-model="useZstd">Zstd（替代 LZ4 路径）</el-checkbox>
            </el-form-item>
            <el-form-item label="耐久性">
              <el-checkbox v-model="balancedDurability">平衡耐久模式</el-checkbox>
              <FieldTip content="更多 fsync 点，略降吞吐；适合关键数据。" />
            </el-form-item>
          </template>
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
.first-backup-hint {
  margin-bottom: 12px;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
.pipeline-section {
  margin-bottom: 12px;
  padding: 12px 14px !important;
}
.pipeline-title {
  margin: 0 0 4px;
  font-size: 12px;
  color: var(--text-soft);
  font-weight: 600;
}
</style>
