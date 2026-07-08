<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { listManifestPage, type ManifestFileDto } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import { collapseIncludePaths } from "@/utils/restorePaths";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";
import PathHistoryDrawer from "@/components/PathHistoryDrawer.vue";

const PAGE_SIZE = 200;

const repo = useRepoStore();
const ui = useUiStore();

const txnId = ref<number | undefined>(undefined);
const search = ref("");
const debouncedSearch = ref("");
const typeFilter = ref<"all" | "file" | "dir" | "symlink">("all");
const loading = ref(false);
const loadingMore = ref(false);
const manifestTxn = ref(0);
const pageTotal = ref(0);
const pageOffset = ref(0);
const totalBytes = ref(0);
const files = ref<ManifestFileDto[]>([]);
const selectedRows = ref<ManifestFileDto[]>([]);
const historyOpen = ref(false);
const historyPath = ref("");

let searchTimer: ReturnType<typeof setTimeout> | null = null;

const TYPE_LABELS: Record<string, string> = {
  file: "文件",
  dir: "目录",
  symlink: "符号链接",
  fifo: "FIFO",
  block: "块设备",
  char: "字符设备",
};

onMounted(() => {
  applyPrefill();
  if (repo.isOpen) void loadFiles(true);
});

onUnmounted(() => {
  if (searchTimer) clearTimeout(searchTimer);
});

watch(
  () => ui.browseTxnPrefill,
  () => applyPrefill()
);

watch(
  () => [repo.isOpen, txnId.value] as const,
  () => {
    if (repo.isOpen) void loadFiles(true);
  }
);

watch(search, (q) => {
  if (searchTimer) clearTimeout(searchTimer);
  searchTimer = setTimeout(() => {
    debouncedSearch.value = q.trim();
  }, 300);
});

watch(debouncedSearch, () => {
  if (repo.isOpen) void loadFiles(true);
});

function applyPrefill() {
  const pre = ui.consumeBrowseTxn();
  if (pre != null && pre > 0) txnId.value = pre;
}

function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  if (n < 1024 * 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(2)} MB`;
  return `${(n / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

function typeLabel(t: string): string {
  return TYPE_LABELS[t] ?? t;
}

const filteredFiles = computed(() => {
  if (typeFilter.value === "all") return files.value;
  return files.value.filter((f) => f.file_type === typeFilter.value);
});

const hasMore = computed(() => files.value.length < pageTotal.value);

const summaryText = computed(() => {
  if (!pageTotal.value && !files.value.length) return "";
  const shown = filteredFiles.value.length;
  const loaded = files.value.length;
  const typeSuffix =
    typeFilter.value !== "all" && shown !== loaded ? `（类型筛选 ${shown}/${loaded}）` : "";
  const sel =
    selectedRows.value.length > 0
      ? ` · 已选 ${collapseIncludePaths(selectedRows.value.map((r) => r.relative_path)).length} 路径`
      : "";
  return `Txn #${manifestTxn.value} · 已加载 ${loaded}/${pageTotal.value} 项${typeSuffix}${sel}`;
});

async function loadFiles(reset = true) {
  if (!repo.isOpen) return;
  if (reset) {
    loading.value = true;
    pageOffset.value = 0;
    files.value = [];
    selectedRows.value = [];
  }
  try {
    const res = await listManifestPage(
      txnId.value || undefined,
      debouncedSearch.value || undefined,
      pageOffset.value,
      PAGE_SIZE
    );
    manifestTxn.value = res.txn_id;
    pageTotal.value = res.count;
    totalBytes.value = res.total_bytes;
    const batch = res.files.slice().sort((a, b) => a.relative_path.localeCompare(b.relative_path));
    if (reset) {
      files.value = batch;
    } else {
      const seen = new Set(files.value.map((f) => f.relative_path));
      for (const f of batch) {
        if (!seen.has(f.relative_path)) files.value.push(f);
      }
      files.value.sort((a, b) => a.relative_path.localeCompare(b.relative_path));
    }
    pageOffset.value = files.value.length;
    if (reset) {
      ui.pushLog(
        `已加载清单 ${files.value.length}/${pageTotal.value} 项 (Txn #${res.txn_id})`,
        "meta"
      );
    }
  } catch (e) {
    if (reset) {
      files.value = [];
      manifestTxn.value = 0;
      pageTotal.value = 0;
      totalBytes.value = 0;
    }
    ui.pushLog(formatGenericError(await enrichError(e), "加载清单失败"), "error");
  } finally {
    loading.value = false;
  }
}

async function loadMore() {
  if (!hasMore.value || loadingMore.value) return;
  loadingMore.value = true;
  try {
    await loadFiles(false);
  } finally {
    loadingMore.value = false;
  }
}

function pickSnapshot(txn: number) {
  txnId.value = txn;
}

function onSelectionChange(rows: ManifestFileDto[]) {
  selectedRows.value = rows;
}

function restoreSelected() {
  if (!selectedRows.value.length) {
    ui.pushLog("请先勾选要恢复的路径", "error");
    return;
  }
  const paths = collapseIncludePaths(selectedRows.value.map((r) => r.relative_path));
  ui.goRestoreWithSelection(manifestTxn.value || txnId.value || 0, paths);
}

function restoreOne(row: ManifestFileDto) {
  ui.goRestoreWithSelection(manifestTxn.value || txnId.value || 0, [row.relative_path]);
}

function goRestore() {
  if (manifestTxn.value > 0) {
    ui.goRestoreWithTxn(manifestTxn.value);
  } else {
    ui.setActivity("restore");
  }
}

function showPathHistory(row: ManifestFileDto) {
  historyPath.value = row.relative_path;
  historyOpen.value = true;
}

function showSearchHistory() {
  const q = search.value.trim();
  if (!q) {
    ui.pushLog("请输入要查询的路径关键词", "error");
    return;
  }
  const hit = files.value.find((f) => f.relative_path.includes(q));
  if (!hit) {
    ui.pushLog("未找到匹配路径（可尝试加载更多或调整前缀）", "error");
    return;
  }
  showPathHistory(hit);
}
</script>

<template>
  <div class="activity-page">
    <section class="panel-card">
      <div class="head">
        <h2>备份内容查询</h2>
        <div class="head-actions">
          <el-button link type="primary" @click="ui.openHelp('activity')">帮助</el-button>
          <el-button size="small" :disabled="!repo.isOpen" :loading="loading" @click="loadFiles(true)">
            刷新
          </el-button>
        </div>
      </div>
      <EmptyState
        v-if="!repo.isOpen"
        title="请先打开仓库"
        action-label="前往仓库页"
        @action="ui.setActivity('repo')"
      />
      <template v-else>
        <el-form inline class="toolbar">
          <el-form-item label="快照 Txn">
            <el-input-number v-model="txnId" :min="0" placeholder="留空=最新" controls-position="right" />
            <FieldTip content="留空查看当前 manifest；填写数字查看历史快照中的文件列表。" />
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
        </el-form>
        <div class="filter-row">
          <el-input
            v-model="search"
            placeholder="路径前缀筛选（服务端分页）"
            clearable
            style="max-width: 320px"
          />
          <el-select v-model="typeFilter" style="width: 120px">
            <el-option label="全部类型" value="all" />
            <el-option label="文件" value="file" />
            <el-option label="目录" value="dir" />
            <el-option label="符号链接" value="symlink" />
          </el-select>
          <el-button size="small" @click="showSearchHistory">路径历史</el-button>
          <el-button size="small" type="primary" :disabled="!selectedRows.length" @click="restoreSelected">
            恢复选中
          </el-button>
          <el-button size="small" @click="goRestore">前往恢复页</el-button>
        </div>
        <p v-if="summaryText" class="summary">{{ summaryText }}</p>
        <el-table
          v-loading="loading"
          :data="filteredFiles"
          size="small"
          stripe
          height="min(520px, calc(100vh - 320px))"
          @selection-change="onSelectionChange"
        >
          <el-table-column type="selection" width="40" />
          <el-table-column prop="relative_path" label="相对路径" min-width="220" show-overflow-tooltip />
          <el-table-column label="类型" width="88">
            <template #default="{ row }">{{ typeLabel(row.file_type) }}</template>
          </el-table-column>
          <el-table-column label="大小" width="96" align="right">
            <template #default="{ row }">{{ formatBytes(row.size) }}</template>
          </el-table-column>
          <el-table-column prop="chunk_count" label="块数" width="64" align="right" />
          <el-table-column label="操作" width="140" fixed="right">
            <template #default="{ row }">
              <el-button link type="primary" size="small" @click="showPathHistory(row)">历史</el-button>
              <el-button link type="primary" size="small" @click="restoreOne(row)">恢复</el-button>
            </template>
          </el-table-column>
        </el-table>
        <div v-if="hasMore" class="load-more">
          <el-button :loading="loadingMore" @click="loadMore">加载更多（{{ files.length }}/{{ pageTotal }}）</el-button>
        </div>
      </template>
    </section>
    <PathHistoryDrawer v-model="historyOpen" :path="historyPath" />
  </div>
</template>

<style scoped>
.activity-page {
  padding: 16px 20px;
  height: 100%;
  overflow: auto;
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
}
.toolbar {
  margin-bottom: 8px;
}
.snap-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.filter-row {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  align-items: center;
  margin-bottom: 8px;
}
.summary {
  margin: 0 0 8px;
  font-size: 12px;
  color: var(--text-soft);
}
.load-more {
  margin-top: 10px;
  text-align: center;
}
</style>
