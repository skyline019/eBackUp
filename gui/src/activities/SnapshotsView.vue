<script setup lang="ts">
import { onMounted, ref, watch } from "vue";
import {
  pruneSnapshots,
  setAuditKey,
  exportDelta,
  importDelta,
  pickFile,
  pickDirectory,
  snapshotReachability,
  type SnapshotDto,
  type SnapshotReachabilityDto,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { confirmDestructive } from "@/utils/confirmDestructive";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";

const repo = useRepoStore();
const ui = useUiStore();

const retentionTiers = ref("1d:7,1w:4,1m:6");
const retainMin = ref(3);
const dryRun = ref(true);
const auditKey = ref("");
const busy = ref(false);
const lastPruneSummary = ref("");
const deltaBaseTxn = ref<number | undefined>(undefined);
const deltaTargetTxn = ref<number | undefined>(undefined);
const deltaBundlePath = ref("");
const deltaPassword = ref("");
const deltaBusy = ref(false);
const importBasePath = ref("");
const importDeltaPath = ref("");
const importOutRepo = ref("");
const importBusy = ref(false);
const reachability = ref<Record<number, SnapshotReachabilityDto>>({});
const reachabilityLoading = ref<Record<number, boolean>>({});

const TIER_PRESETS = [
  { label: "7 日保留", value: "1d:7" },
  { label: "标准 GFS", value: "1d:7,1w:4,1m:6" },
  { label: "轻量（3+2+2）", value: "1d:3,1w:2,1m:2" },
];

onMounted(async () => {
  if (repo.isOpen) await refresh();
});

watch(
  () => repo.snapshots.map((s) => s.txn_id),
  (ids) => {
    for (const id of ids) void loadReachability(id);
  },
  { immediate: true }
);

async function loadReachability(txnId: number) {
  if (!repo.isOpen || reachability.value[txnId] || reachabilityLoading.value[txnId]) return;
  reachabilityLoading.value = { ...reachabilityLoading.value, [txnId]: true };
  try {
    const rep = await snapshotReachability(txnId);
    reachability.value = { ...reachability.value, [txnId]: rep };
  } catch {
    /* optional */
  } finally {
    const next = { ...reachabilityLoading.value };
    delete next[txnId];
    reachabilityLoading.value = next;
  }
}

function isReachable(txnId: number): boolean | undefined {
  return reachability.value[txnId]?.reachable;
}

async function refresh() {
  reachability.value = {};
  try {
    await repo.refreshSnapshots();
    ui.pushLog(`快照列表已刷新 (${repo.snapshots.length})`, "meta");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e)), "error");
  }
}

function applyPreset(value: string) {
  retentionTiers.value = value;
}

function restoreFromRow(txnId: number) {
  if (isReachable(txnId) === false) {
    ui.pushLog(`Txn ${txnId} 增量链不可达，请先 verify-chain 或维护`, "error");
    return;
  }
  ui.goRestoreWithTxn(txnId);
}

function browseFromRow(txnId: number) {
  ui.goBrowseWithTxn(txnId);
}

function diffFromRow(txnId: number) {
  const snaps = repo.snapshots;
  const idx = snaps.findIndex((s) => s.txn_id === txnId);
  const other = idx >= 0 && idx + 1 < snaps.length ? snaps[idx + 1].txn_id : snaps[0]?.txn_id;
  if (other && other !== txnId) {
    ui.goDiffWithTxns(other, txnId);
  } else {
    ui.pushLog("需要至少两个快照才能对比", "error");
  }
}

function onSnapRowClick(row: SnapshotDto) {
  restoreFromRow(row.txn_id);
}

function formatImmutable(until?: number) {
  if (!until) return "";
  return new Date(until * 1000).toLocaleDateString();
}

async function runPrune() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  if (!dryRun.value) {
    const ok = await confirmDestructive(
      "执行快照 Prune",
      `将按策略「${retentionTiers.value}」删除过期快照（最少保留 ${retainMin.value} 份）。此操作不可撤销。`
    );
    if (!ok) return;
    if (auditKey.value.trim()) {
      await setAuditKey(auditKey.value.trim());
    }
  }
  busy.value = true;
  try {
    const res = await pruneSnapshots(retentionTiers.value, retainMin.value, dryRun.value);
    ui.setTaskResult(res);
    const kept = Number(res.kept_count ?? 0);
    const pruned = Number(res.pruned_count ?? 0);
    lastPruneSummary.value = `保留 ${kept}，删除 ${pruned}`;
    ui.pushLog(`Prune ${dryRun.value ? "(dry-run) " : ""}${lastPruneSummary.value}`, "success");
    if (!dryRun.value) await repo.refreshSnapshots();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "Prune 失败"), "error");
  } finally {
    busy.value = false;
  }
}

function pickDeltaBaseFromRow(txnId: number) {
  deltaBaseTxn.value = txnId;
  if (!deltaTargetTxn.value) {
    const latest = repo.snapshots[0]?.txn_id;
    if (latest && latest !== txnId) deltaTargetTxn.value = latest;
  }
}

async function browseDeltaBundle() {
  const p = await pickFile();
  if (p) deltaBundlePath.value = p;
}

async function runExportDelta() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  if (!deltaBaseTxn.value || deltaBaseTxn.value <= 0) {
    ui.pushLog("请选择基准 Txn", "error");
    return;
  }
  if (!deltaBundlePath.value.trim()) {
    ui.pushLog("请选择输出 .ebb 路径", "error");
    return;
  }
  deltaBusy.value = true;
  try {
    const res = await exportDelta(
      deltaBaseTxn.value,
      deltaBundlePath.value.trim(),
      deltaTargetTxn.value || undefined,
      deltaPassword.value.trim() || undefined
    );
    ui.setTaskResult(res);
    ui.pushLog(
      `增量包已导出：chunks=${res.chunk_count ?? 0} bytes=${res.bytes ?? 0}`,
      "success"
    );
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "导出增量包失败"), "error");
  } finally {
    deltaBusy.value = false;
  }
}

async function browseImportBase() {
  const p = await pickDirectory();
  if (p) importBasePath.value = p;
}

async function browseImportDelta() {
  const p = await pickFile();
  if (p) importDeltaPath.value = p;
}

async function browseImportOut() {
  const p = await pickDirectory();
  if (p) importOutRepo.value = p;
}

async function runImportDelta() {
  if (!importBasePath.value.trim() || !importDeltaPath.value.trim() || !importOutRepo.value.trim()) {
    ui.pushLog("请填写基准 repo/bundle、增量包与目标 repo 路径", "error");
    return;
  }
  importBusy.value = true;
  try {
    const res = await importDelta(
      importBasePath.value.trim(),
      importDeltaPath.value.trim(),
      importOutRepo.value.trim()
    );
    ui.setTaskResult(res);
    ui.pushLog("增量包导入完成", "success");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "导入增量包失败"), "error");
  } finally {
    importBusy.value = false;
  }
}
</script>

<template>
  <div class="activity-page">
    <section class="panel-card">
      <div class="head">
        <h2>快照列表</h2>
        <div class="head-actions">
          <el-button link type="primary" @click="ui.openHelp('activity')">帮助</el-button>
          <el-button size="small" :disabled="!repo.isOpen" @click="refresh">刷新</el-button>
        </div>
      </div>
      <el-table
        v-if="repo.snapshots.length"
        :data="repo.snapshots"
        size="small"
        stripe
        highlight-current-row
        class="snap-table"
        @row-click="onSnapRowClick"
      >
        <el-table-column prop="txn_id" label="Txn" width="80" />
        <el-table-column label="时间">
          <template #default="{ row }">
            {{ new Date(row.created_at_unix * 1000).toLocaleString() }}
          </template>
        </el-table-column>
        <el-table-column prop="file_count" label="文件数" width="90" />
        <el-table-column label="链可达" width="90">
          <template #default="{ row }">
            <el-tag v-if="isReachable(row.txn_id) === true" size="small" type="success">可达</el-tag>
            <el-tag v-else-if="isReachable(row.txn_id) === false" size="small" type="danger">缺块</el-tag>
            <span v-else class="muted">…</span>
          </template>
        </el-table-column>
        <el-table-column label="作业" width="90">
          <template #default="{ row }">{{ row.job_id || "—" }}</template>
        </el-table-column>
        <el-table-column label="不可变" width="110">
          <template #default="{ row }">
            <span v-if="row.immutable_until_unix" title="WORM 保护中">🔒 {{ formatImmutable(row.immutable_until_unix) }}</span>
            <span v-else class="muted">—</span>
          </template>
        </el-table-column>
        <el-table-column prop="manifest_crc32" label="CRC32" width="100" />
        <el-table-column label="" width="180">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click.stop="browseFromRow(row.txn_id)">
              内容
            </el-button>
            <el-button
              link
              type="primary"
              size="small"
              :disabled="isReachable(row.txn_id) === false"
              @click.stop="restoreFromRow(row.txn_id)"
            >
              恢复
            </el-button>
            <el-button link size="small" @click.stop="diffFromRow(row.txn_id)">对比</el-button>
            <el-button link size="small" @click.stop="pickDeltaBaseFromRow(row.txn_id)">Δ 基准</el-button>
          </template>
        </el-table-column>
      </el-table>
      <p v-else class="muted">尚无快照或未打开仓库 — 请先完成一次备份</p>
      <p v-if="repo.snapshots.length" class="table-hint">点击行恢复；「Δ 基准」设为增量导出基准 txn</p>
    </section>

    <section class="panel-card">
      <h2>增量包 (EBB Delta)</h2>
      <el-form label-width="120px">
        <el-form-item label="基准 Txn">
          <el-input-number v-model="deltaBaseTxn" :min="1" placeholder="base-at" />
        </el-form-item>
        <el-form-item label="目标 Txn">
          <el-input-number v-model="deltaTargetTxn" :min="0" placeholder="留空=最新" />
        </el-form-item>
        <el-form-item label="输出 .ebb">
          <div class="row">
            <el-input v-model="deltaBundlePath" placeholder="增量包路径" />
            <el-button @click="browseDeltaBundle">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="加密密码">
          <el-input v-model="deltaPassword" type="password" show-password placeholder="可选" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="deltaBusy" :disabled="!repo.isOpen" @click="runExportDelta">
            导出增量包
          </el-button>
        </el-form-item>
      </el-form>
      <el-divider />
      <el-form label-width="120px">
        <el-form-item label="基准 repo">
          <div class="row">
            <el-input v-model="importBasePath" placeholder="基准仓库或 bundle" />
            <el-button @click="browseImportBase">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="增量 .ebb">
          <div class="row">
            <el-input v-model="importDeltaPath" placeholder="delta bundle" />
            <el-button @click="browseImportDelta">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="目标 repo">
          <div class="row">
            <el-input v-model="importOutRepo" placeholder="输出仓库路径" />
            <el-button @click="browseImportOut">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item>
          <el-button :loading="importBusy" @click="runImportDelta">导入增量包</el-button>
          <FieldTip content="等价于 eb import BASE DELTA OUT_REPO（delta 模式）。" />
        </el-form-item>
      </el-form>
    </section>

    <section class="panel-card">
      <h2>保留策略 (GFS)</h2>
      <el-form label-width="120px">
        <el-form-item label="预设">
          <el-button
            v-for="p in TIER_PRESETS"
            :key="p.value"
            size="small"
            @click="applyPreset(p.value)"
          >
            {{ p.label }}
          </el-button>
        </el-form-item>
        <el-form-item label="Tiers">
          <el-input v-model="retentionTiers" placeholder="1d:7,1w:4,1m:6" />
          <FieldTip content="格式：1d:7 表示每日 7 份，1w:4 每周 4 份，1m:6 每月 6 份。" />
        </el-form-item>
        <el-form-item label="最少保留">
          <el-input-number v-model="retainMin" :min="1" :max="100" />
        </el-form-item>
        <el-form-item label="Dry run">
          <el-switch v-model="dryRun" />
          <FieldTip content="开启时仅模拟，不删除快照；确认后再关闭并执行。" />
        </el-form-item>
        <el-form-item v-if="!dryRun" label="审计密钥">
          <el-input
            v-model="auditKey"
            type="password"
            show-password
            placeholder="WORM 仓库 Prune 可能需要"
          />
          <FieldTip content="不可变仓库删除快照需 RAR 审计授权；与验证页密钥相同。" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="busy" :disabled="!repo.isOpen" @click="runPrune">
            执行 Prune
          </el-button>
        </el-form-item>
        <p v-if="lastPruneSummary" class="muted">上次结果：{{ lastPruneSummary }}</p>
      </el-form>
    </section>
  </div>
</template>

<style scoped>
.activity-page {
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  overflow: auto;
  height: 100%;
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
  align-items: center;
}
.panel-card h2 {
  margin: 0 0 12px;
  font-size: 15px;
}
.muted {
  color: var(--text-soft);
  font-size: 13px;
}
.table-hint {
  margin: 8px 0 0;
  font-size: 11px;
  color: var(--text-soft);
}
.snap-table {
  cursor: pointer;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
</style>
