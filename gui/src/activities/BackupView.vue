<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import {
  runBackup,
  pickDirectory,
  pickFile,
  loadFilterFile,
  setFilterJson,
  suggestExcludeFilters,
  setPassword,
  pathExists,
  getBackupReport,
  setBackupHooks,
  enqueueJob,
  jobQueueStatus,
  runJobQueue,
  BUILTIN_PLUGINS,
  type BackupJobDto,
  type ExcludeFilterSuggestionDto,
  type JobQueueStatusDto,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useJobStore } from "@/stores/jobStore";
import { useUiStore } from "@/stores/uiStore";
import { useTaskStore } from "@/stores/taskStore";
import { setActivityRunner } from "@/composables/useActivityRunners";
import { enrichError, formatBackupError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";
import BackupPipelineViz from "@/components/BackupPipelineViz.vue";
import { isTauriRuntime } from "@/utils/tauriRuntime";
import { ElMessageBox } from "element-plus";

const repo = useRepoStore();
const jobs = useJobStore();
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
const preBackupCmd = ref("");
const postBackupCmd = ref("");
const selectedPlugins = ref<string[]>([]);
const showAdvanced = ref(false);
const running = ref(false);
const jobDialogOpen = ref(false);
const editingJob = ref<BackupJobDto | null>(null);
const jobForm = ref<BackupJobDto>({
  id: "",
  name: "",
  source_path: "",
  retention_tag: 0,
  immutability_days: 0,
  worm: false,
  exclude_globs: [],
  exclude_paths: [],
  plugins: [],
  window_start: "",
  window_end: "",
  deadline_grace_seconds: 300,
  durability_adaptive: false,
});
const excludeGlobsText = ref("");
const excludePathsText = ref("");
const jobSuggestions = ref<ExcludeFilterSuggestionDto[]>([]);
const adhocSuggestions = ref<ExcludeFilterSuggestionDto[]>([]);
const suggestBusy = ref(false);
const adhocSuggestBusy = ref(false);
const includeIdeDirs = ref(false);
const sessionExcludePaths = ref<string[]>([]);
const sessionExcludeGlobs = ref<string[]>([]);
const showJobExcludeHint = ref(false);
const queueStatus = ref<JobQueueStatusDto | null>(null);
const queueBusy = ref(false);

const FLAG_LZ4 = 0x0001;
const FLAG_PIPELINE = 0x0002;
const FLAG_ENCRYPT = 0x0008;
const FLAG_COMPRESS_AUTO = 0x0020;
const FLAG_COMPRESS_ZSTD = 0x0040;
const FLAG_BALANCED_DURABILITY = 0x0080;

const isFirstBackup = computed(
  () => repo.isOpen && repo.snapshots.length === 0 && (repo.info?.manifest_bytes ?? 0) === 0
);

const isWindows =
  typeof navigator !== "undefined" && /Windows/i.test(navigator.userAgent);

const visiblePlugins = computed(() =>
  BUILTIN_PLUGINS.filter((p) => p.platforms === "all" || isWindows)
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
  if (repo.isOpen) await jobs.refreshJobs();
  await refreshQueueStatus();
  unregRunner = setActivityRunner("backup-run", run);
});

onUnmounted(() => unregRunner?.());

watch(
  () => [repo.isOpen, repo.snapshots.length, repo.info?.manifest_bytes] as const,
  () => syncIncrementalDefault()
);

watch(
  () => repo.isOpen,
  async (open) => {
    if (open) {
      await jobs.refreshJobs();
      await refreshQueueStatus();
    } else {
      jobs.clear();
      queueStatus.value = null;
    }
  }
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

async function browseJobSource() {
  const p = await pickDirectory();
  if (p) jobForm.value.source_path = p;
}

function linesToList(text: string): string[] {
  return text
    .split(/\r?\n/)
    .map((s) => s.trim())
    .filter(Boolean);
}

function mergeUniqueLines(existing: string, additions: string[]): string {
  const set = new Set(linesToList(existing));
  for (const a of additions) set.add(a);
  return [...set].join("\n");
}

function buildExistingFilterFromJobForm() {
  const exclude_paths = linesToList(excludePathsText.value);
  const exclude_globs = linesToList(excludeGlobsText.value);
  if (!exclude_paths.length && !exclude_globs.length) return undefined;
  return { exclude_paths, exclude_globs };
}

async function analyzeJobSource() {
  const source = jobForm.value.source_path.trim();
  if (!source) {
    ui.pushLog("请先填写作业源路径", "error");
    return;
  }
  if (!(await pathExists(source))) {
    ui.pushLog("源目录不存在", "error");
    return;
  }
  suggestBusy.value = true;
  jobSuggestions.value = [];
  try {
    const res = await suggestExcludeFilters(source, {
      existing: buildExistingFilterFromJobForm(),
      includeIdeDirs: includeIdeDirs.value,
    });
    if (!res.ok && res.ok !== undefined) {
      ui.pushLog("分析失败", "error");
      return;
    }
    jobSuggestions.value = res.items ?? [];
    ui.pushLog(`发现 ${jobSuggestions.value.length} 条排除建议`, "meta");
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  } finally {
    suggestBusy.value = false;
  }
}

function applyJobSuggestion(row: ExcludeFilterSuggestionDto) {
  if (row.apply_as === "exclude_path") {
    excludePathsText.value = mergeUniqueLines(excludePathsText.value, [row.pattern]);
  } else {
    excludeGlobsText.value = mergeUniqueLines(excludeGlobsText.value, [row.pattern]);
  }
}

function applyAllJobSuggestions() {
  for (const row of jobSuggestions.value) applyJobSuggestion(row);
}

async function analyzeAdhocSource() {
  const source = sourcePath.value.trim();
  if (!source) {
    ui.pushLog("请先填写源目录", "error");
    return;
  }
  if (!(await pathExists(source))) {
    ui.pushLog("源目录不存在", "error");
    return;
  }
  adhocSuggestBusy.value = true;
  adhocSuggestions.value = [];
  try {
    const existing =
      sessionExcludePaths.value.length || sessionExcludeGlobs.value.length
        ? {
            exclude_paths: [...sessionExcludePaths.value],
            exclude_globs: [...sessionExcludeGlobs.value],
          }
        : undefined;
    const res = await suggestExcludeFilters(source, {
      includeIdeDirs: includeIdeDirs.value,
      existing,
    });
    adhocSuggestions.value = res.items ?? [];
    ui.pushLog(`发现 ${adhocSuggestions.value.length} 条排除建议`, "meta");
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  } finally {
    adhocSuggestBusy.value = false;
  }
}

async function applyAdhocSuggestions() {
  if (!adhocSuggestions.value.length) return;
  for (const row of adhocSuggestions.value) {
    applyAdhocSuggestion(row, false);
  }
  await syncAdhocSessionFilter();
}

function applyAdhocSuggestion(row: ExcludeFilterSuggestionDto, sync = true) {
  if (row.apply_as === "exclude_path") {
    if (!sessionExcludePaths.value.includes(row.pattern)) {
      sessionExcludePaths.value = [...sessionExcludePaths.value, row.pattern];
    }
  } else if (!sessionExcludeGlobs.value.includes(row.pattern)) {
    sessionExcludeGlobs.value = [...sessionExcludeGlobs.value, row.pattern];
  }
  if (sync) void syncAdhocSessionFilter();
}

async function syncAdhocSessionFilter() {
  if (!sessionExcludePaths.value.length && !sessionExcludeGlobs.value.length) return;
  try {
    await setFilterJson({
      exclude_paths: sessionExcludePaths.value,
      exclude_globs: sessionExcludeGlobs.value,
    });
    ui.pushLog("已采纳建议到会话过滤器", "success");
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  }
}

function resetJobForm(job?: BackupJobDto) {
  if (job) {
    editingJob.value = job;
    jobForm.value = {
      ...job,
      exclude_globs: [...(job.exclude_globs ?? [])],
      exclude_paths: [...(job.exclude_paths ?? [])],
      plugins: [...(job.plugins ?? [])],
      window_start: job.window_start ?? "",
      window_end: job.window_end ?? "",
      deadline_grace_seconds: job.deadline_grace_seconds ?? 300,
      durability_adaptive: job.durability_adaptive ?? false,
    };
  } else {
    editingJob.value = null;
    jobForm.value = {
      id: "",
      name: "",
      source_path: sourcePath.value.trim(),
      retention_tag: 0,
      immutability_days: 0,
      worm: false,
      exclude_globs: [...sessionExcludeGlobs.value],
      exclude_paths: [...sessionExcludePaths.value],
      plugins: [...selectedPlugins.value],
      window_start: "",
      window_end: "",
      deadline_grace_seconds: 300,
      durability_adaptive: false,
    };
  }
  excludeGlobsText.value = (jobForm.value.exclude_globs ?? []).join("\n");
  excludePathsText.value = (jobForm.value.exclude_paths ?? []).join("\n");
  jobSuggestions.value = [];
  showJobExcludeHint.value =
    !editingJob.value &&
    !excludePathsText.value.trim() &&
    !excludeGlobsText.value.trim();
}

function openNewJobDialog() {
  resetJobForm();
  jobDialogOpen.value = true;
}

function openEditJob(job: BackupJobDto) {
  resetJobForm(job);
  jobDialogOpen.value = true;
}

async function saveJob() {
  const id = jobForm.value.id.trim();
  const name = jobForm.value.name.trim();
  const source = jobForm.value.source_path.trim();
  if (!id || !name || !source) {
    ui.pushLog("作业 id、名称、源路径均为必填", "error");
    return;
  }
  if (!(await pathExists(source))) {
    ui.pushLog("源目录不存在", "error");
    return;
  }
  const globs = linesToList(excludeGlobsText.value);
  const paths = linesToList(excludePathsText.value);
  try {
    await jobs.upsertJob({
      ...jobForm.value,
      id,
      name,
      source_path: source,
      exclude_globs: globs,
      exclude_paths: paths,
      plugins: jobForm.value.plugins ?? [],
      window_start: jobForm.value.window_start?.trim() || undefined,
      window_end: jobForm.value.window_end?.trim() || undefined,
      deadline_grace_seconds: jobForm.value.deadline_grace_seconds,
      durability_adaptive: jobForm.value.durability_adaptive,
    });
    jobDialogOpen.value = false;
    ui.pushLog(`作业已保存: ${id}`, "success");
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  }
}

async function removeJob(job: BackupJobDto) {
  try {
    await ElMessageBox.confirm(`删除作业「${job.name}」(${job.id})？`, "确认删除", {
      type: "warning",
    });
    await jobs.deleteJob(job.id);
    ui.pushLog(`已删除作业 ${job.id}`, "meta");
  } catch {
    /* cancelled */
  }
}

async function saveCurrentAsJob() {
  const src = sourcePath.value.trim();
  if (!src) {
    ui.pushLog("请先填写源目录", "error");
    return;
  }
  resetJobForm();
  jobForm.value.source_path = src;
  jobForm.value.name = src.split(/[/\\]/).filter(Boolean).pop() ?? "backup";
  jobForm.value.id = jobForm.value.name.toLowerCase().replace(/[^a-z0-9_-]+/g, "_");
  jobForm.value.plugins = [...selectedPlugins.value];
  jobForm.value.exclude_paths = [...sessionExcludePaths.value];
  jobForm.value.exclude_globs = [...sessionExcludeGlobs.value];
  excludePathsText.value = jobForm.value.exclude_paths.join("\n");
  excludeGlobsText.value = jobForm.value.exclude_globs.join("\n");
  showJobExcludeHint.value =
    !excludePathsText.value.trim() && !excludeGlobsText.value.trim();
  jobDialogOpen.value = true;
}

async function finishBackupRun(stats: Awaited<ReturnType<typeof runBackup>>, logPrefix: string) {
  ui.setTaskResult(stats);
  ui.lastResultJson = JSON.stringify(stats, null, 2);
  ui.outputTab = "results";
  await repo.refreshInfo();
  await repo.refreshSnapshots();
  const latestTxn = repo.snapshots[0]?.txn_id;
  if (latestTxn) {
    try {
      const report = await getBackupReport(latestTxn);
      if (report.ok) {
        ui.setBackupReportResult(report);
        const parts: string[] = [];
        if (report.job_id) parts.push(`作业 ${report.job_id}`);
        if ((report.locked ?? 0) > 0) parts.push(`锁定 ${report.locked}`);
        if ((report.permission_denied ?? 0) > 0) parts.push(`权限 ${report.permission_denied}`);
        if ((report.skipped ?? 0) > 0) parts.push(`跳过 ${report.skipped}`);
        if ((report.hook_failed ?? 0) > 0) parts.push(`Hook ${report.hook_failed}`);
        if ((report.issues?.length ?? 0) > 0 || parts.length) {
          ui.pushLog(
            parts.length ? `备份报告：${parts.join("，")}` : `备份报告：${report.issues.length} 项未完全备份`,
            "meta"
          );
        }
        if (report.durability_downgraded) {
          ui.pushLog("备份窗口：已降级 durability（Balanced）", "meta");
        }
        if (report.window_truncated) {
          ui.pushLog("备份窗口：已截断（部分快照）", "error");
        }
      }
    } catch {
      /* report optional */
    }
  }
  const reusePct =
    stats.chunks_written + stats.chunks_reused > 0
      ? Math.round((100 * stats.chunks_reused) / (stats.chunks_written + stats.chunks_reused))
      : 0;
  ui.pushLog(
    `${logPrefix} — 文件 ${stats.files_processed}，写入块 ${stats.chunks_written}，复用 ${stats.chunks_reused}（${reusePct}%）`,
    "success"
  );
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
    const pre = preBackupCmd.value.trim();
    const post = postBackupCmd.value.trim();
    if (pre || post || selectedPlugins.value.length) {
      await setBackupHooks(
        pre || undefined,
        post || undefined,
        selectedPlugins.value.length ? selectedPlugins.value : undefined
      );
    }
    const stats = await runBackup(src, incremental.value, buildFlags());
    repo.setLastSource(src);
    await finishBackupRun(stats, "备份完成");
    if (!isTauriRuntime()) task.finish(true);
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
    if (!isTauriRuntime()) task.finish(false);
  } finally {
    running.value = false;
  }
}

async function refreshQueueStatus() {
  if (!repo.isOpen) {
    queueStatus.value = null;
    return;
  }
  try {
    queueStatus.value = await jobQueueStatus();
  } catch {
    queueStatus.value = null;
  }
}

async function enqueueJobRow(job: BackupJobDto) {
  if (!repo.isOpen) return;
  queueBusy.value = true;
  ui.pushLog(`入队作业: ${job.id}`, "cmd");
  try {
    await enqueueJob(job.id, incremental.value, buildFlags());
    await refreshQueueStatus();
    ui.pushLog(`作业 ${job.id} 已加入队列`, "success");
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  } finally {
    queueBusy.value = false;
  }
}

async function runQueueNext() {
  if (!repo.isOpen) return;
  queueBusy.value = true;
  ui.pushLog("运行队列下一项…", "cmd");
  try {
    await runJobQueue(false, buildFlags());
    await refreshQueueStatus();
    await repo.refreshSnapshots();
    ui.pushLog("队列下一项已完成", "success");
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  } finally {
    queueBusy.value = false;
  }
}

async function drainQueue() {
  if (!repo.isOpen) return;
  try {
    await ElMessageBox.confirm(
      "将依次运行队列中全部待处理作业，可能耗时较长。继续？",
      "Drain 全部队列",
      { type: "warning" }
    );
  } catch {
    return;
  }
  queueBusy.value = true;
  ui.pushLog("Drain 队列全部…", "cmd");
  try {
    await runJobQueue(true, buildFlags());
    await refreshQueueStatus();
    await repo.refreshSnapshots();
    ui.pushLog("队列已全部 Drain 完成", "success");
  } catch (e) {
    ui.pushLog(formatBackupError(await enrichError(e)), "error");
  } finally {
    queueBusy.value = false;
  }
}

async function runJobRow(job: BackupJobDto) {
  if (!repo.isOpen) return;
  running.value = true;
  if (!isTauriRuntime()) task.start("backup");
  ui.pushLog(`运行作业: ${job.id} (${job.source_path})`, "cmd");
  try {
    if (useEncrypt.value && password.value) {
      await setPassword(password.value);
    }
    const pre = preBackupCmd.value.trim();
    const post = postBackupCmd.value.trim();
    const plugins = job.plugins ?? [];
    if (pre || post || plugins.length) {
      await setBackupHooks(
        pre || undefined,
        post || undefined,
        plugins.length ? plugins : undefined
      );
    }
    const stats = await jobs.runJob(job.id, incremental.value, buildFlags());
    await finishBackupRun(stats, `作业 ${job.id} 完成`);
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
            <el-checkbox v-model="incremental" :disabled="isFirstBackup">增量备份</el-checkbox>
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
            <el-form-item label="Pre Hook">
              <el-input v-model="preBackupCmd" placeholder="备份前 shell 命令（可选，自行承担风险）" />
            </el-form-item>
            <el-form-item label="Post Hook">
              <el-input v-model="postBackupCmd" placeholder="备份后 shell 命令（可选）" />
            </el-form-item>
            <el-form-item label="垂直插件">
              <el-checkbox-group v-model="selectedPlugins">
                <el-checkbox
                  v-for="p in visiblePlugins"
                  :key="p.id"
                  :label="p.id"
                >
                  {{ p.label }}
                </el-checkbox>
              </el-checkbox-group>
              <FieldTip content="SQLite checkpoint 全平台；Registry/VHDX 仅 Windows 且可能需要管理员权限。" />
            </el-form-item>
            <el-form-item label="建议排除">
              <div class="suggest-block">
                <el-checkbox v-model="includeIdeDirs" label="包含 IDE 目录" />
                <el-button size="small" :loading="adhocSuggestBusy" @click="analyzeAdhocSource">
                  分析源目录
                </el-button>
                <el-button
                  v-if="adhocSuggestions.length"
                  size="small"
                  type="primary"
                  @click="applyAdhocSuggestions"
                >
                  全部采纳到会话 filter
                </el-button>
              </div>
              <table v-if="adhocSuggestions.length" class="suggest-table">
                <thead>
                  <tr>
                    <th>类型</th>
                    <th>规则</th>
                    <th>原因</th>
                    <th>示例</th>
                    <th></th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="(row, i) in adhocSuggestions" :key="i">
                    <td>{{ row.kind }}</td>
                    <td>{{ row.pattern }}</td>
                    <td>{{ row.reason }}</td>
                    <td>{{ row.example_path }}</td>
                    <td>
                      <el-button link type="primary" size="small" @click="applyAdhocSuggestion(row)">
                        添加
                      </el-button>
                    </td>
                  </tr>
                </tbody>
              </table>
            </el-form-item>
          </template>
          <el-form-item>
            <el-button type="primary" :loading="running" @click="run">运行备份</el-button>
            <el-button @click="saveCurrentAsJob">保存为作业…</el-button>
          </el-form-item>
        </el-form>
      </template>
    </section>

    <section v-if="repo.isOpen" class="panel-card queue-section">
      <div class="head-row">
        <h2>作业队列</h2>
        <el-button size="small" :disabled="queueBusy" @click="refreshQueueStatus">刷新状态</el-button>
      </div>
      <p class="muted queue-hint">
        「入队」将作业写入持久化队列；「运行」立即执行，不经过队列。「Drain 全部」依次跑完 pending 项。
      </p>
      <div v-if="queueStatus" class="queue-status">
        <span>待处理: <strong>{{ queueStatus.pending_count }}</strong></span>
        <span>状态: <strong>{{ queueStatus.state }}</strong></span>
        <span v-if="queueStatus.jobs?.length">
          队首: <strong>{{ queueStatus.jobs[0]?.job_id }}</strong>
        </span>
      </div>
      <div class="queue-actions">
        <el-button
          size="small"
          :loading="queueBusy"
          :disabled="!queueStatus?.pending_count"
          @click="runQueueNext"
        >
          运行下一项
        </el-button>
        <el-button
          size="small"
          type="warning"
          :loading="queueBusy"
          :disabled="!queueStatus?.pending_count"
          @click="drainQueue"
        >
          Drain 全部
        </el-button>
      </div>
    </section>

    <section v-if="repo.isOpen" class="panel-card jobs-section">
      <div class="head-row">
        <h2>已保存作业</h2>
        <el-button size="small" type="primary" @click="openNewJobDialog">新建作业</el-button>
      </div>
      <el-table v-if="jobs.jobs.length" :data="jobs.jobs" size="small" stripe class="jobs-table">
        <el-table-column prop="name" label="名称" min-width="100" />
        <el-table-column prop="id" label="ID" width="100" />
        <el-table-column prop="source_path" label="源路径" min-width="160" show-overflow-tooltip />
        <el-table-column label="保留" width="70">
          <template #default="{ row }">{{ row.retention_tag ?? 0 }}</template>
        </el-table-column>
        <el-table-column label="WORM" width="70">
          <template #default="{ row }">
            <el-tag v-if="row.worm" size="small" type="warning">是</el-tag>
            <span v-else class="muted">—</span>
          </template>
        </el-table-column>
        <el-table-column label="" width="260">
          <template #default="{ row }">
            <el-button link type="primary" size="small" :disabled="running || queueBusy" @click="enqueueJobRow(row)">
              入队
            </el-button>
            <el-button link type="primary" size="small" :disabled="running" @click="runJobRow(row)">
              运行
            </el-button>
            <el-button link size="small" @click="openEditJob(row)">编辑</el-button>
            <el-button link type="danger" size="small" @click="removeJob(row)">删除</el-button>
          </template>
        </el-table-column>
      </el-table>
      <p v-else class="muted">尚无保存的作业 — 使用「保存为作业」或「新建作业」添加</p>
    </section>

    <el-dialog
      v-model="jobDialogOpen"
      :title="editingJob ? '编辑作业' : '新建作业'"
      width="520px"
      destroy-on-close
    >
      <el-form label-width="120px">
        <el-form-item label="ID" required>
          <el-input v-model="jobForm.id" :disabled="!!editingJob" placeholder="docs" />
        </el-form-item>
        <el-form-item label="名称" required>
          <el-input v-model="jobForm.name" placeholder="Documents" />
        </el-form-item>
        <el-form-item label="源路径" required>
          <div class="row">
            <el-input v-model="jobForm.source_path" />
            <el-button @click="browseJobSource">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="排除路径">
          <el-alert
            v-if="showJobExcludeHint"
            type="info"
            :closable="true"
            show-icon
            title="建议先点击「分析源目录」查看可排除项，确认后再保存作业。"
            class="job-exclude-hint"
            @close="showJobExcludeHint = false"
          />
          <div class="suggest-block">
            <el-checkbox v-model="includeIdeDirs" label="包含 IDE 目录" />
            <el-button size="small" :loading="suggestBusy" @click="analyzeJobSource">分析源目录</el-button>
            <el-button
              v-if="jobSuggestions.length"
              size="small"
              type="primary"
              @click="applyAllJobSuggestions"
            >
              全部采纳
            </el-button>
          </div>
          <el-input
            v-model="excludePathsText"
            type="textarea"
            :rows="2"
            placeholder="每行一个路径前缀，如 node_modules"
          />
        </el-form-item>
        <el-form-item label="排除 glob">
          <el-input
            v-model="excludeGlobsText"
            type="textarea"
            :rows="3"
            placeholder="每行一个，如 *.tmp"
          />
        </el-form-item>
        <el-form-item v-if="jobSuggestions.length" label="建议">
          <table class="suggest-table">
            <thead>
              <tr>
                <th>类型</th>
                <th>规则</th>
                <th>原因</th>
                <th></th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(row, i) in jobSuggestions" :key="i">
                <td>{{ row.kind }}</td>
                <td>{{ row.pattern }}</td>
                <td>{{ row.reason }}</td>
                <td>
                  <el-button link type="primary" size="small" @click="applyJobSuggestion(row)">
                    添加
                  </el-button>
                </td>
              </tr>
            </tbody>
          </table>
        </el-form-item>
        <el-form-item label="retention_tag">
          <el-input-number v-model="jobForm.retention_tag" :min="0" :max="9999" />
        </el-form-item>
        <el-form-item label="不可变天数">
          <el-input-number v-model="jobForm.immutability_days" :min="0" :max="3650" />
        </el-form-item>
        <el-form-item label="WORM 仓库">
          <el-checkbox v-model="jobForm.worm">启用不可变策略</el-checkbox>
          <FieldTip content="写入后快照受保护；Prune 删除需审计密钥授权。" />
        </el-form-item>
        <el-form-item label="垂直插件">
          <el-checkbox-group v-model="jobForm.plugins">
            <el-checkbox
              v-for="p in visiblePlugins"
              :key="p.id"
              :label="p.id"
            >
              {{ p.label }}
            </el-checkbox>
          </el-checkbox-group>
        </el-form-item>
        <el-form-item label="备份窗口">
          <div class="window-fields">
            <el-input v-model="jobForm.window_start" placeholder="开始 HH:MM（如 02:00）" />
            <el-input v-model="jobForm.window_end" placeholder="结束 HH:MM（如 06:00）" />
            <el-input-number
              v-model="jobForm.deadline_grace_seconds"
              :min="60"
              :max="3600"
            />
            <el-checkbox v-model="jobForm.durability_adaptive">
              接近截止时降级 durability（Balanced）
            </el-checkbox>
          </div>
          <FieldTip content="仅在窗口内运行作业；schedule/队列在窗外跳过。grace 秒内触发 durability 自适应；超时截断并写入报告。" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="jobDialogOpen = false">取消</el-button>
        <el-button type="primary" :loading="jobs.busy" @click="saveJob">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<style scoped>
.activity-page {
  padding: 16px 20px;
  overflow: auto;
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 16px;
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
.suggest-block {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 8px;
}
.window-fields {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin-bottom: 8px;
}
.job-exclude-hint {
  margin-bottom: 8px;
}
.suggest-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 11px;
  margin-top: 6px;
}
.suggest-table th,
.suggest-table td {
  border: 1px solid var(--shell-line, #2a3348);
  padding: 4px 6px;
  text-align: left;
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
.jobs-section {
  flex-shrink: 0;
}
.jobs-table {
  width: 100%;
}
.muted {
  color: var(--text-soft);
  font-size: 13px;
}
.queue-hint {
  margin: 0 0 10px;
}
.queue-status {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  margin-bottom: 10px;
  font-size: 13px;
}
.queue-actions {
  display: flex;
  gap: 8px;
}
</style>
