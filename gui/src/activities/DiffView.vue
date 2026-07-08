<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import { diffSnapshots } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import EmptyState from "@/components/EmptyState.vue";

interface DiffAddedRow {
  path: string;
  subset_merkle?: string;
  proof_steps?: number;
}

interface DiffModifiedRow {
  path: string;
  size_a: number;
  size_b: number;
  content_hash_a?: string;
  content_hash_b?: string;
  proof_steps?: number;
}

const repo = useRepoStore();
const ui = useUiStore();

const txnA = ref<number | undefined>(undefined);
const txnB = ref<number | undefined>(undefined);
const loading = ref(false);
const rawResult = ref<Record<string, unknown> | null>(null);

const addedRows = computed<DiffAddedRow[]>(() => {
  const arr = rawResult.value?.added;
  if (!Array.isArray(arr)) return [];
  return arr.map((item) => {
    if (typeof item === "string") return { path: item };
    const o = item as Record<string, unknown>;
    const proof = o.merkle_proof;
    return {
      path: String(o.path ?? ""),
      subset_merkle: o.subset_merkle as string | undefined,
      proof_steps: Array.isArray(proof) ? proof.length : undefined,
    };
  });
});

const removedRows = computed(() => {
  const arr = rawResult.value?.removed;
  if (!Array.isArray(arr)) return [] as string[];
  return arr.map((x) => String(x));
});

const modifiedRows = computed<DiffModifiedRow[]>(() => {
  const arr = rawResult.value?.modified;
  if (!Array.isArray(arr)) return [];
  return arr.map((item) => {
    const o = item as Record<string, unknown>;
    const proof = o.merkle_proof;
    return {
      path: String(o.path ?? ""),
      size_a: Number(o.size_a ?? 0),
      size_b: Number(o.size_b ?? 0),
      content_hash_a: o.content_hash_a as string | undefined,
      content_hash_b: o.content_hash_b as string | undefined,
      proof_steps: Array.isArray(proof) ? proof.length : undefined,
    };
  });
});

const summaryLine = computed(() => {
  if (!rawResult.value) return "";
  const reuse = rawResult.value.chunk_reuse_ratio;
  const root = rawResult.value.diff_merkle_root_hex;
  const parts = [
    `+${addedRows.value.length}`,
    `-${removedRows.value.length}`,
    `~${modifiedRows.value.length}`,
  ];
  if (typeof reuse === "number") parts.push(`块复用率 ${(reuse * 100).toFixed(1)}%`);
  if (typeof root === "string" && root) parts.push(`diff root ${root.slice(0, 12)}…`);
  return parts.join(" · ");
});

onMounted(() => applyPrefill());

watch(
  () => ui.diffTxnPrefill,
  () => applyPrefill()
);

function applyPrefill() {
  const pre = ui.consumeDiffTxns();
  if (pre) {
    txnA.value = pre.txnA;
    txnB.value = pre.txnB;
  }
}

async function runDiff() {
  if (!repo.isOpen || txnA.value == null || txnB.value == null) {
    ui.pushLog("请选择两个快照 Txn", "error");
    return;
  }
  loading.value = true;
  try {
    const res = await diffSnapshots(txnA.value, txnB.value);
    rawResult.value = res;
    ui.setTaskResult(res);
    ui.lastResultJson = JSON.stringify(res, null, 2);
    ui.outputTab = "results";
    ui.pushLog(`Diff #${txnA.value} vs #${txnB.value}: ${summaryLine.value}`, "success");
  } catch (e) {
    rawResult.value = null;
    ui.pushLog(formatGenericError(await enrichError(e), "Diff 失败"), "error");
  } finally {
    loading.value = false;
  }
}

async function exportJson() {
  if (!rawResult.value) return;
  const text = JSON.stringify(rawResult.value, null, 2);
  try {
    await navigator.clipboard.writeText(text);
    ui.pushLog("Diff JSON 已复制到剪贴板", "success");
  } catch {
    ui.pushLog("复制失败", "error");
  }
}
</script>

<template>
  <div class="activity-page">
    <section class="panel-card">
      <div class="head">
        <h2>快照对比（可证明 Diff）</h2>
        <el-button link type="primary" @click="ui.openHelp('activity')">帮助</el-button>
      </div>
      <EmptyState
        v-if="!repo.isOpen"
        title="请先打开仓库"
        action-label="前往仓库页"
        @action="ui.setActivity('repo')"
      />
      <template v-else>
        <el-form inline class="toolbar">
          <el-form-item label="Txn A">
            <el-input-number v-model="txnA" :min="1" controls-position="right" />
          </el-form-item>
          <el-form-item label="Txn B">
            <el-input-number v-model="txnB" :min="1" controls-position="right" />
          </el-form-item>
          <el-form-item>
            <el-button type="primary" :loading="loading" @click="runDiff">对比</el-button>
            <el-button :disabled="!rawResult" @click="exportJson">导出 JSON</el-button>
          </el-form-item>
        </el-form>
        <p v-if="summaryLine" class="summary">{{ summaryLine }}</p>
        <template v-if="rawResult">
          <h3 class="section-title">新增 ({{ addedRows.length }})</h3>
          <el-table :data="addedRows" size="small" stripe max-height="200" empty-text="无">
            <el-table-column prop="path" label="路径" min-width="200" show-overflow-tooltip />
            <el-table-column prop="subset_merkle" label="子树 Merkle" min-width="120" show-overflow-tooltip />
            <el-table-column prop="proof_steps" label="证明步数" width="88" align="right" />
          </el-table>
          <h3 class="section-title">删除 ({{ removedRows.length }})</h3>
          <el-table :data="removedRows.map((p) => ({ path: p }))" size="small" stripe max-height="160" empty-text="无">
            <el-table-column prop="path" label="路径" min-width="240" show-overflow-tooltip />
          </el-table>
          <h3 class="section-title">修改 ({{ modifiedRows.length }})</h3>
          <el-table :data="modifiedRows" size="small" stripe max-height="200" empty-text="无">
            <el-table-column prop="path" label="路径" min-width="160" show-overflow-tooltip />
            <el-table-column prop="size_a" label="大小 A" width="88" align="right" />
            <el-table-column prop="size_b" label="大小 B" width="88" align="right" />
            <el-table-column prop="proof_steps" label="证明步数" width="88" align="right" />
          </el-table>
        </template>
      </template>
    </section>
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
.toolbar {
  margin-bottom: 8px;
}
.summary {
  margin: 0 0 12px;
  font-size: 12px;
  color: var(--text-soft);
}
.section-title {
  margin: 16px 0 8px;
  font-size: 13px;
  font-weight: 600;
}
</style>
