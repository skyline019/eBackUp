<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import {
  runRestoreEx,
  previewRestore,
  previewInPlace,
  applyInPlace,
  pickDirectory,
  pickFile,
  setPassword,
  loadFilterFile,
  type InPlacePreviewDto,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { enrichError, formatRestoreError } from "@/utils/errorMessages";
import {
  buildFilterJson,
  buildRemapJson,
  type RestoreLayoutMode,
  type RestoreConflictPolicy,
  type AclRestorePolicy,
  type ReparseRestorePolicy,
} from "@/utils/restorePaths";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";
import { ElMessageBox } from "element-plus";

const repo = useRepoStore();
const ui = useUiStore();

type RestoreMode = "new_dir" | "in_place";
type InPlaceConflictPolicy = "skip" | "fail" | "overwrite";
type InPlaceOrphanPolicy = "skip" | "delete";

const restoreMode = ref<RestoreMode>("new_dir");
const destPath = ref("");
const txnId = ref<number | undefined>(undefined);
const password = ref("");
const filterPath = ref("");
const skipContentVerify = ref(false);
const running = ref(false);
const previewLoading = ref(false);
const selectedPaths = ref<string[]>([]);
const layoutMode = ref<RestoreLayoutMode>("keep");
const stripPrefix = ref("");
const mapFrom = ref("");
const mapTo = ref("");
const conflictPolicy = ref<RestoreConflictPolicy>("fail");
const inPlaceConflictPolicy = ref<InPlaceConflictPolicy>("skip");
const inPlaceOrphanPolicy = ref<InPlaceOrphanPolicy>("skip");
const baseTxnId = ref<number | undefined>(undefined);
const inPlaceEntryFilter = ref<"all" | "conflicts" | "changes" | "adds">("all");
const inPlaceDryRun = ref(false);
const aclPolicy = ref<AclRestorePolicy>("inherit");
const reparsePolicy = ref<ReparseRestorePolicy>("skip");
const previewText = ref("");
const inPlacePreview = ref<InPlacePreviewDto | null>(null);

const FLAG_SKIP_CONTENT_VERIFY = 0x0001;

const hasSelection = computed(() => selectedPaths.value.length > 0);
const isInPlace = computed(() => restoreMode.value === "in_place");

const inPlaceEntries = computed(() => {
  const rows = inPlacePreview.value?.entries ?? [];
  if (inPlaceEntryFilter.value === "conflicts") {
    return rows.filter((r) => r.action === "conflict" || r.action === "both_changed");
  }
  if (inPlaceEntryFilter.value === "changes") {
    return rows.filter((r) => r.action === "modify" || r.action === "both_changed");
  }
  if (inPlaceEntryFilter.value === "adds") {
    return rows.filter((r) => r.action === "add");
  }
  return rows;
});

function buildInPlaceOptionsJson(extra?: Record<string, unknown>) {
  const opts: Record<string, unknown> = { ...extra };
  if (baseTxnId.value != null && baseTxnId.value > 0) opts.base_txn_id = baseTxnId.value;
  if (inPlaceDryRun.value) opts.dry_run = true;
  return Object.keys(opts).length ? JSON.stringify(opts) : undefined;
}

const inPlaceSummaryText = computed(() => {
  const s = inPlacePreview.value?.summary;
  if (!s) return "";
  const parts = [
    `新增 ${s.add_count}`,
    `修改 ${s.modify_count}`,
    `未变 ${s.unchanged_count}`,
    `冲突 ${s.conflict_count}`,
  ];
  if (s.both_changed_count != null && s.both_changed_count > 0) {
    parts.push(`双方变更 ${s.both_changed_count}`);
  }
  if (s.orphan_count != null && s.orphan_count > 0) {
    parts.push(`孤儿 ${s.orphan_count}`);
  }
  if (inPlacePreview.value?.three_way) {
    parts.push(`三路 base=#${inPlacePreview.value.base_txn_id ?? 0}`);
  }
  parts.push(formatBytes(s.bytes_to_write));
  return parts.join(" · ");
});

onMounted(() => {
  applyPrefill();
});

watch(
  () => [ui.restoreTxnPrefill, ui.restoreSelectionPrefill] as const,
  () => applyPrefill()
);

watch(
  [restoreMode, selectedPaths, layoutMode, stripPrefix, mapFrom, mapTo, destPath, txnId, baseTxnId],
  () => {
    void refreshPreview();
  }
);

function applyPrefill() {
  const sel = ui.consumeRestoreSelection();
  if (sel) {
    txnId.value = sel.txnId;
    selectedPaths.value = sel.includePaths;
  } else {
    const pre = ui.consumeRestoreTxn();
    if (pre != null && pre > 0) txnId.value = pre;
  }
}

async function browseDest() {
  const p = await pickDirectory();
  if (p) destPath.value = p;
}

async function browseFilter() {
  const p = await pickFile();
  if (p) filterPath.value = p;
}

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${(n / 1024 / 1024).toFixed(2)} MiB`;
}

function filterJsonForSelection(): string | undefined {
  return hasSelection.value ? buildFilterJson(selectedPaths.value) : undefined;
}

async function refreshPreview() {
  if (!repo.isOpen) {
    previewText.value = "";
    inPlacePreview.value = null;
    return;
  }
  if (isInPlace.value) {
    if (!destPath.value.trim()) {
      inPlacePreview.value = null;
      return;
    }
    previewLoading.value = true;
    try {
      if (password.value) await setPassword(password.value);
      const filterJson = filterJsonForSelection();
      inPlacePreview.value = await previewInPlace(
        destPath.value.trim(),
        txnId.value || undefined,
        filterJson,
        buildInPlaceOptionsJson()
      );
    } catch {
      inPlacePreview.value = null;
    } finally {
      previewLoading.value = false;
    }
    previewText.value = "";
    return;
  }
  inPlacePreview.value = null;
  if (!hasSelection.value && !filterPath.value.trim()) {
    previewText.value = "";
    return;
  }
  try {
    const filterJson = filterJsonForSelection();
    if (filterJson) {
      const res = await previewRestore(txnId.value || undefined, filterJson);
      previewText.value = `预览：${res.file_count} 文件，${res.dir_count} 目录，${formatBytes(res.total_bytes)}`;
    }
  } catch {
    previewText.value = "";
  }
}

async function runInPlace(conflictPolicy = inPlaceConflictPolicy.value) {
  if (!destPath.value.trim()) {
    ui.pushLog("请选择就地恢复目标目录（live 源树）", "error");
    return;
  }
  if (conflictPolicy === "overwrite") {
    try {
      await ElMessageBox.confirm(
        "覆盖模式将用快照内容覆盖目标路径上的冲突文件，是否继续？",
        "就地恢复确认",
        { type: "warning", confirmButtonText: "继续", cancelButtonText: "取消" }
      );
    } catch {
      return;
    }
  }
  if (inPlaceOrphanPolicy.value === "delete") {
    try {
      await ElMessageBox.confirm(
        "删除孤儿文件将移除快照中不存在、但 live 源树上仍存在的普通文件（不删目录）。是否继续？",
        "孤儿文件删除确认",
        { type: "warning", confirmButtonText: "继续删除", cancelButtonText: "取消" }
      );
    } catch {
      return;
    }
  }
  running.value = true;
  ui.pushLog(`就地恢复 → ${destPath.value}`, "cmd");
  try {
    if (password.value) await setPassword(password.value);
    const filterJson = filterJsonForSelection();
    const res = await applyInPlace(
      destPath.value.trim(),
      txnId.value || undefined,
      conflictPolicy,
      filterJson,
      inPlaceOrphanPolicy.value,
      buildInPlaceOptionsJson()
    );
    inPlacePreview.value = res;
    ui.setTaskResult(res);
    ui.lastResultJson = JSON.stringify(res, null, 2);
    ui.outputTab = "results";
    ui.pushLog(inPlaceDryRun.value ? "就地恢复 dry-run 完成" : "就地恢复完成", "success");
  } catch (e) {
    ui.pushLog(formatRestoreError(await enrichError(e)), "error");
  } finally {
    running.value = false;
  }
}

async function run() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  if (isInPlace.value) {
    await runInPlace();
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
    let filterJson: string | undefined;
    if (hasSelection.value) {
      filterJson = buildFilterJson(selectedPaths.value);
    } else if (filterPath.value.trim()) {
      await loadFilterFile(filterPath.value.trim());
    }
    const needsRemap =
      layoutMode.value !== "keep" ||
      aclPolicy.value !== "inherit" ||
      reparsePolicy.value !== "skip";
    const remapJson = needsRemap
      ? buildRemapJson({
          mode: layoutMode.value,
          strip_prefix: stripPrefix.value.trim() || undefined,
          map_from: mapFrom.value.trim() || undefined,
          map_to: mapTo.value.trim() || undefined,
          conflict: conflictPolicy.value,
          acl_policy: aclPolicy.value !== "inherit" ? aclPolicy.value : undefined,
          reparse_policy:
            reparsePolicy.value !== "skip" ? reparsePolicy.value : undefined,
        })
      : undefined;
    let flags = 0;
    if (skipContentVerify.value) flags |= FLAG_SKIP_CONTENT_VERIFY;
    const res = await runRestoreEx(
      destPath.value.trim(),
      txnId.value || undefined,
      flags,
      filterJson,
      remapJson
    );
    ui.setTaskResult(res);
    const report = (res as { acceptance_report?: Record<string, unknown> }).acceptance_report;
    if (report) {
      ui.setAcceptanceResult(report);
    } else {
      ui.lastResultJson = JSON.stringify(res, null, 2);
      ui.outputTab = "results";
    }
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

function clearSelection() {
  selectedPaths.value = [];
  previewText.value = "";
  inPlacePreview.value = null;
}

function actionLabel(action: string) {
  const map: Record<string, string> = {
    add: "新增",
    modify: "修改",
    unchanged: "未变",
    conflict: "冲突",
    both_changed: "双方变更",
    skip: "跳过",
    orphan: "孤儿",
  };
  return map[action] ?? action;
}

async function applyBulkNonConflicts() {
  inPlaceConflictPolicy.value = "skip";
  inPlaceDryRun.value = false;
  await runInPlace("skip");
}

async function applyBulkOverwriteConflicts() {
  try {
    await ElMessageBox.confirm(
      "将全部冲突项按快照内容覆盖写回，是否继续？",
      "批量覆盖冲突",
      { type: "warning", confirmButtonText: "覆盖", cancelButtonText: "取消" }
    );
  } catch {
    return;
  }
  inPlaceDryRun.value = false;
  await runInPlace("overwrite");
}

async function applyBulkKeepLiveConflicts() {
  inPlaceDryRun.value = false;
  await runInPlace("skip");
}

async function runDryRun() {
  inPlaceDryRun.value = true;
  await runInPlace(inPlaceConflictPolicy.value);
  inPlaceDryRun.value = false;
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
        <el-form-item label="恢复模式">
          <el-radio-group v-model="restoreMode">
            <el-radio value="new_dir">新目录</el-radio>
            <el-radio value="in_place">就地恢复</el-radio>
          </el-radio-group>
          <FieldTip
            v-if="isInPlace"
            content="就地恢复将快照内容写回 live 源树；路径 remap 请使用「新目录」模式。"
          />
        </el-form-item>
        <el-form-item v-if="hasSelection" label="已选路径">
          <div class="chips">
            <el-tag v-for="p in selectedPaths" :key="p" size="small" type="info">{{ p }}</el-tag>
            <el-button link type="primary" @click="clearSelection">清除</el-button>
          </div>
          <FieldTip content="来自「内容」页的多选；将仅恢复这些路径及其子树。" />
        </el-form-item>
        <el-form-item :label="isInPlace ? '目标源树' : '目标目录'">
          <div class="row">
            <el-input
              v-model="destPath"
              :placeholder="isInPlace ? 'live 源目录（就地写回）' : '还原输出位置'"
            />
            <el-button @click="browseDest">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="快照 Txn">
          <el-input-number v-model="txnId" :min="0" placeholder="留空=最新" />
          <span class="hint">留空表示最新快照</span>
        </el-form-item>
        <el-form-item v-if="isInPlace" label="Base Txn">
          <el-input-number v-model="baseTxnId" :min="0" placeholder="留空=自动" />
          <span class="hint">三路合并基准快照；留空取 target 前一快照</span>
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
        <el-form-item v-if="!hasSelection && !isInPlace" label="过滤器">
          <div class="row">
            <el-input v-model="filterPath" placeholder="可选 .filter 文件" />
            <el-button @click="browseFilter">浏览</el-button>
          </div>
        </el-form-item>
        <template v-if="isInPlace">
          <el-form-item label="冲突策略">
            <el-select v-model="inPlaceConflictPolicy" style="width: 160px">
              <el-option label="跳过冲突" value="skip" />
              <el-option label="遇冲突报错" value="fail" />
              <el-option label="覆盖" value="overwrite" />
            </el-select>
            <FieldTip content="与布局 remap 冲突策略独立；仅作用于就地写回时的内容冲突。" />
          </el-form-item>
          <el-form-item label="孤儿策略">
            <el-select v-model="inPlaceOrphanPolicy" style="width: 160px">
              <el-option label="仅报告" value="skip" />
              <el-option label="删除孤儿文件" value="delete" />
            </el-select>
            <FieldTip content="孤儿 = live 源树上存在但快照未覆盖的普通文件；删除需二次确认。" />
          </el-form-item>
          <el-form-item label="就地预览">
            <div class="inplace-preview-block">
              <div v-if="previewLoading" class="preview muted">加载预览…</div>
              <div v-else-if="inPlaceSummaryText" class="preview">{{ inPlaceSummaryText }}</div>
              <div v-else class="preview muted">填写目标源树后自动预览</div>
              <div v-if="inPlaceEntries.length" class="bulk-row">
                <el-select v-model="inPlaceEntryFilter" style="width: 140px">
                  <el-option label="全部" value="all" />
                  <el-option label="仅冲突" value="conflicts" />
                  <el-option label="仅变更" value="changes" />
                  <el-option label="仅新增" value="adds" />
                </el-select>
                <el-button size="small" @click="applyBulkNonConflicts">应用非冲突</el-button>
                <el-button size="small" @click="applyBulkOverwriteConflicts">冲突用快照</el-button>
                <el-button size="small" @click="applyBulkKeepLiveConflicts">冲突保留 live</el-button>
                <el-button size="small" @click="runDryRun">Dry-run</el-button>
              </div>
              <table v-if="inPlaceEntries.length" class="entries-table">
                <thead>
                  <tr>
                    <th>路径</th>
                    <th>动作</th>
                    <th>Base</th>
                    <th>Live</th>
                    <th>说明</th>
                  </tr>
                </thead>
                <tbody>
                  <tr
                    v-for="(row, i) in inPlaceEntries"
                    :key="i"
                    :class="{
                      'row-conflict':
                        row.action === 'conflict' || row.action === 'both_changed',
                    }"
                  >
                    <td class="path-cell">{{ row.path }}</td>
                    <td>{{ actionLabel(row.action) }}</td>
                    <td>{{ row.base_action || "—" }}</td>
                    <td>{{ row.live_state || "—" }}</td>
                    <td>{{ row.reason || "—" }}</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </el-form-item>
        </template>
        <template v-else>
          <el-form-item label="布局重整">
            <el-radio-group v-model="layoutMode">
              <el-radio value="keep">保持原结构</el-radio>
              <el-radio value="strip_prefix">去前缀</el-radio>
              <el-radio value="flatten">扁平化</el-radio>
              <el-radio value="remap_prefix">前缀映射</el-radio>
            </el-radio-group>
          </el-form-item>
          <el-form-item v-if="layoutMode === 'strip_prefix'" label="去前缀">
            <el-input v-model="stripPrefix" placeholder="例如 project/src" />
          </el-form-item>
          <el-form-item v-if="layoutMode === 'remap_prefix'" label="映射">
            <div class="row">
              <el-input v-model="mapFrom" placeholder="原前缀" />
              <el-input v-model="mapTo" placeholder="新前缀（可空）" />
            </div>
          </el-form-item>
          <el-form-item
            v-if="layoutMode === 'flatten' || layoutMode === 'remap_prefix'"
            label="冲突策略"
          >
            <el-select v-model="conflictPolicy" style="width: 140px">
              <el-option label="报错" value="fail" />
              <el-option label="跳过" value="skip" />
              <el-option label="后缀 _N" value="suffix" />
            </el-select>
          </el-form-item>
          <el-form-item label="ACL 策略">
            <el-select v-model="aclPolicy" style="width: 180px">
              <el-option label="继承目标目录" value="inherit" />
              <el-option label="保留备份 ACL" value="preserve" />
              <el-option label="尽力保留 ACL" value="best_effort" />
              <el-option label="跳过 ACL" value="skip" />
            </el-select>
            <FieldTip content="Windows 元数据：从 manifest v5 恢复安全描述符（需备份时采集）。" />
          </el-form-item>
          <el-form-item label="联接点恢复">
            <el-select v-model="reparsePolicy" style="width: 180px">
              <el-option label="跳过联接点" value="skip" />
              <el-option label="重建联接点" value="recreate" />
            </el-select>
            <FieldTip content="junction 目录：skip 仅恢复目标内容；recreate 在目标路径重建联接点。" />
          </el-form-item>
          <el-form-item v-if="previewText" label="预览">
            <span class="preview">{{ previewText }}</span>
          </el-form-item>
          <el-form-item label="高级">
            <el-checkbox v-model="skipContentVerify">跳过内容校验</el-checkbox>
          </el-form-item>
        </template>
        <el-form-item>
          <el-button type="primary" :loading="running" @click="run">
            {{ isInPlace ? "应用就地恢复" : "开始恢复" }}
          </el-button>
          <el-button v-if="isInPlace" :loading="previewLoading" @click="refreshPreview">
            刷新预览
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
.bulk-row {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin: 8px 0;
  align-items: center;
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
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  align-items: center;
}
.hint {
  margin-left: 8px;
  font-size: 12px;
  color: var(--text-soft);
}
.preview {
  font-size: 13px;
  color: var(--text-soft);
}
.muted {
  opacity: 0.75;
}
.snap-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.inplace-preview-block {
  width: 100%;
}
.entries-table {
  width: 100%;
  margin-top: 8px;
  border-collapse: collapse;
  font-size: 12px;
}
.entries-table th,
.entries-table td {
  border: 1px solid var(--border-subtle, #333);
  padding: 4px 8px;
  text-align: left;
}
.entries-table th {
  background: var(--surface-raised, rgba(255, 255, 255, 0.04));
}
.path-cell {
  word-break: break-all;
  max-width: 280px;
}
.row-conflict td {
  color: var(--el-color-warning);
}
</style>
