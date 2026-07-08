<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import {
  compactRepo,
  gcOrphans,
  orphanExplain,
  runMaintenanceWizard,
  syncMaintenanceCheck,
  type OrphanExplainDto,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { confirmDestructive } from "@/utils/confirmDestructive";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";
import {
  refreshSyncStatus,
  syncStatusData,
  syncStatusLoading,
} from "@/composables/useBackupAlerts";

const repo = useRepoStore();
const ui = useUiStore();

const wizardStep = ref(0);
const compactDryRun = ref(true);
const gcDryRun = ref(true);
const verifyAfter = ref(false);
const busy = ref(false);
const lastCompactSummary = ref("");
const lastGcSummary = ref("");
const lastWizardSummary = ref("");
const orphanExplainReport = ref<OrphanExplainDto | null>(null);
const explainBusy = ref(false);
const orphanBytesBeforeWizard = ref<number | null>(null);

const REASON_LABELS: Record<string, string> = {
  unreferenced: "无引用",
  tombstoned: "墓碑",
  interrupted_hint: "中断提示",
};

const explainBars = computed(() => {
  const r = orphanExplainReport.value;
  if (!r) return [];
  const total = Math.max(
    1,
    r.unreferenced_count + r.tombstoned_count + r.interrupted_hint_count
  );
  return [
    { key: "unreferenced", label: REASON_LABELS.unreferenced, count: r.unreferenced_count, pct: (r.unreferenced_count / total) * 100 },
    { key: "tombstoned", label: REASON_LABELS.tombstoned, count: r.tombstoned_count, pct: (r.tombstoned_count / total) * 100 },
    { key: "interrupted_hint", label: REASON_LABELS.interrupted_hint, count: r.interrupted_hint_count, pct: (r.interrupted_hint_count / total) * 100 },
  ];
});

const WIZARD_STEPS = ["分析", "Prune", "GC", "Compact", "完成"];

onMounted(async () => {
  if (repo.isOpen) {
    await refreshSyncStatus();
    await loadOrphanExplain();
    try {
      const check = await syncMaintenanceCheck();
      maintenanceBlockReason.value = check.blocked ? check.reason : "";
    } catch {
      maintenanceBlockReason.value = "";
    }
  }
});

async function refreshSyncCard() {
  await refreshSyncStatus();
  try {
    const check = await syncMaintenanceCheck();
    maintenanceBlockReason.value = check.blocked ? check.reason : "";
  } catch {
    maintenanceBlockReason.value = "";
  }
}

function reasonLabel(reason: string) {
  return REASON_LABELS[reason] ?? reason;
}

function chunkPrefix(hex: string) {
  return hex.length > 12 ? `${hex.slice(0, 12)}…` : hex;
}

const maintenanceBlockReason = ref("");
const syncStatus = computed(() => syncStatusData.value);

async function ensureSyncBeforeDestructive(actionLabel: string): Promise<boolean> {
  if (!repo.isOpen) return false;
  try {
    const check = await syncMaintenanceCheck();
    maintenanceBlockReason.value = check.blocked ? check.reason : "";
    if (check.blocked) {
      ui.pushLog(`${actionLabel} 已阻止：${check.reason}`, "error");
      return false;
    }
    return true;
  } catch {
    return true;
  }
}

async function loadOrphanExplain() {
  if (!repo.isOpen) return;
  explainBusy.value = true;
  try {
    orphanExplainReport.value = await orphanExplain(64);
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "孤儿解释加载失败"), "error");
  } finally {
    explainBusy.value = false;
  }
}

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  if (n < 1024 * 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MiB`;
  return `${(n / 1024 / 1024 / 1024).toFixed(2)} GiB`;
}

function summarizeCompact(res: Record<string, unknown>) {
  const before = Number(res.physical_before ?? 0);
  const after = Number(res.physical_after ?? 0);
  const ratio = Number(res.ampl_ratio_after ?? res.ampl_ratio ?? 0);
  return `物理 ${formatBytes(before)} → ${formatBytes(after)}，放大率 ${ratio.toFixed(2)}×`;
}

function summarizeGc(res: Record<string, unknown>) {
  const orphans = Number(res.orphan_count ?? 0);
  const tomb = Number(res.tombstoned_count ?? 0);
  return `孤儿块 ${orphans}，墓碑 ${tomb}`;
}

function summarizeWizard(res: Record<string, unknown>) {
  const before = res.repo_stats as Record<string, unknown> | undefined;
  const after = res.stats_after as Record<string, unknown> | undefined;
  const amplBefore = Number(before?.ampl_ratio ?? 0);
  const amplAfter = Number(after?.ampl_ratio ?? 0);
  const orphanBefore = Number(before?.orphan_bytes ?? 0);
  const orphanAfter = Number(after?.orphan_bytes ?? 0);
  return `放大率 ${amplBefore.toFixed(2)}× → ${amplAfter.toFixed(2)}×，孤儿 ${formatBytes(orphanBefore)} → ${formatBytes(orphanAfter)}`;
}

async function refreshStats() {
  try {
    await repo.refreshInfo();
    ui.pushLog("统计已刷新", "meta");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e)), "error");
  }
}

async function runWizardDryRun() {
  if (!repo.isOpen) return;
  busy.value = true;
  wizardStep.value = 1;
  orphanBytesBeforeWizard.value = repo.info?.orphan_bytes ?? null;
  await loadOrphanExplain();
  try {
    const res = await runMaintenanceWizard(
      JSON.stringify({ dry_run_only: true, verify_after: false })
    );
    ui.setTaskResult(res);
    lastWizardSummary.value = summarizeWizard(res);
    wizardStep.value = 4;
    ui.pushLog(`重整向导 Dry run：${lastWizardSummary.value}`, "meta");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "向导 Dry run 失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function runWizardLive() {
  if (!repo.isOpen) return;
  if (!(await ensureSyncBeforeDestructive("仓库重整"))) return;
  const ok = await confirmDestructive(
    "执行仓库重整",
    "将依次执行 Prune → GC → Compact（按引擎策略）。建议先 Dry run。"
  );
  if (!ok) return;
  busy.value = true;
  wizardStep.value = 2;
  try {
    const res = await runMaintenanceWizard(
      JSON.stringify({ dry_run_only: false, verify_after: verifyAfter.value })
    );
    ui.setTaskResult(res);
    lastWizardSummary.value = summarizeWizard(res);
    wizardStep.value = 4;
    ui.pushLog(`仓库重整完成：${lastWizardSummary.value}`, "success");
    await repo.refreshInfo();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "仓库重整失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function runCompact() {
  if (!repo.isOpen) return;
  if (!compactDryRun.value && !(await ensureSyncBeforeDestructive("Compact"))) return;
  if (!compactDryRun.value) {
    const ok = await confirmDestructive(
      "执行 Compact",
      "将重写 EbPack 布局以降低放大率，耗时与仓库大小相关。建议先 Dry run 查看预估。"
    );
    if (!ok) return;
  }
  busy.value = true;
  try {
    const res = await compactRepo(compactDryRun.value);
    ui.setTaskResult(res);
    lastCompactSummary.value = summarizeCompact(res);
    ui.pushLog(`Compact ${compactDryRun.value ? "(dry-run) " : ""}${lastCompactSummary.value}`, "success");
    if (!compactDryRun.value) await repo.refreshInfo();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "Compact 失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function runGc() {
  if (!repo.isOpen) return;
  if (!gcDryRun.value && !(await ensureSyncBeforeDestructive("孤儿 GC"))) return;
  if (!gcDryRun.value) {
    const ok = await confirmDestructive(
      "执行孤儿块 GC",
      "将删除无引用的 chunk 数据。请确认已备份且了解回收范围。"
    );
    if (!ok) return;
  }
  busy.value = true;
  try {
    const res = await gcOrphans(gcDryRun.value);
    ui.setTaskResult(res);
    lastGcSummary.value = summarizeGc(res);
    ui.pushLog(`GC ${gcDryRun.value ? "(dry-run) " : ""}${lastGcSummary.value}`, "success");
    if (!gcDryRun.value) await repo.refreshInfo();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "GC 失败"), "error");
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <div class="activity-page">
    <el-alert
      v-if="maintenanceBlockReason"
      type="warning"
      show-icon
      :closable="false"
      class="sync-block-alert"
      :title="maintenanceBlockReason"
    />
    <section v-if="repo.isOpen" class="panel-card sync-status-card">
      <div class="head">
        <h2>同步状态</h2>
        <div class="head-actions">
          <el-button link type="primary" :loading="syncStatusLoading" @click="refreshSyncCard">
            刷新
          </el-button>
        </div>
      </div>
      <FieldTip
        content="维护操作（Compact / GC / 重整）在同步有待传 chunk 或维护锁定时会被阻止。"
      />
      <template v-if="syncStatus">
        <dl class="status-dl">
          <dt>待同步 txn</dt>
          <dd>{{ syncStatus.pending_txn || "—" }}</dd>
          <dt>待传 chunk</dt>
          <dd>{{ syncStatus.pending_chunk_count }}</dd>
          <dt>失败 chunk</dt>
          <dd>{{ syncStatus.failed_chunks ?? syncStatus.pending_chunk_count }}</dd>
          <dt>维护锁定</dt>
          <dd>{{ syncStatus.maintenance_blocked ? "是" : "否" }}</dd>
        </dl>
      </template>
      <p v-else class="hint">暂无同步状态（未配置 sync 或尚未导出）。</p>
    </section>

    <section v-if="repo.info" class="panel-card stats-grid">
      <div class="head">
        <h2>仓库统计</h2>
        <div class="head-actions">
          <el-button link type="primary" @click="ui.openHelp('activity')">帮助</el-button>
          <el-button size="small" @click="refreshStats">刷新</el-button>
        </div>
      </div>
      <div class="cards">
        <div class="stat-card">
          <span class="label">物理占用</span>
          <span class="value">{{ formatBytes(repo.info.physical_bytes) }}</span>
        </div>
        <div class="stat-card">
          <span class="label">有效数据</span>
          <span class="value">{{ formatBytes(repo.info.live_bytes) }}</span>
        </div>
        <div class="stat-card">
          <span class="label">孤儿块</span>
          <span class="value">{{ formatBytes(repo.info.orphan_bytes) }}</span>
        </div>
        <div class="stat-card">
          <span class="label">放大率</span>
          <span class="value">{{ repo.info.ampl_ratio.toFixed(2) }}×</span>
        </div>
      </div>
    </section>
    <EmptyState
      v-else
      title="请先打开仓库"
      action-label="前往仓库页"
      @action="ui.setActivity('repo')"
    />

    <section v-if="repo.isOpen" class="panel-card orphan-explain-card">
      <div class="head">
        <h2>孤儿块解释</h2>
        <div class="head-actions">
          <el-button size="small" :loading="explainBusy" @click="loadOrphanExplain">分析</el-button>
        </div>
      </div>
      <FieldTip
        content="Prune 快照 → 产生无引用 chunk → GC 回收。可在快照页查看增量链可达性。"
      />
      <p class="flow-line">Prune → 孤儿块 → GC → Compact</p>
      <template v-if="orphanExplainReport">
        <div class="explain-summary">
          <span>总计 {{ orphanExplainReport.total_orphans }} 块</span>
          <span>{{ formatBytes(orphanExplainReport.total_orphan_bytes) }}</span>
        </div>
        <div v-if="orphanBytesBeforeWizard != null" class="wizard-compare">
          向导前孤儿占用：{{ formatBytes(orphanBytesBeforeWizard) }} → 当前
          {{ formatBytes(repo.info?.orphan_bytes ?? 0) }}
        </div>
        <div class="reason-bars">
          <div v-for="bar in explainBars" :key="bar.key" class="reason-row">
            <span class="reason-label">{{ bar.label }}</span>
            <div class="reason-track">
              <div class="reason-fill" :style="{ width: `${bar.pct}%` }" />
            </div>
            <span class="reason-count">{{ bar.count }}</span>
          </div>
        </div>
        <el-table
          v-if="orphanExplainReport.samples.length"
          :data="orphanExplainReport.samples"
          size="small"
          class="sample-table"
          max-height="220"
        >
          <el-table-column label="Chunk" min-width="120">
            <template #default="{ row }">
              <code>{{ chunkPrefix(row.chunk_hex) }}</code>
            </template>
          </el-table-column>
          <el-table-column label="原因" width="100">
            <template #default="{ row }">{{ reasonLabel(row.reason) }}</template>
          </el-table-column>
          <el-table-column label="字节" width="90">
            <template #default="{ row }">{{ formatBytes(row.bytes) }}</template>
          </el-table-column>
          <el-table-column prop="last_referenced_txn" label="末引用 Txn" width="100" />
        </el-table>
      </template>
      <p v-else class="summary-line">点击「分析」加载孤儿块分类与样本。</p>
    </section>

    <section class="panel-card wizard-card">
      <h2>仓库重整向导</h2>
      <el-steps :active="wizardStep" finish-status="success" simple>
        <el-step v-for="s in WIZARD_STEPS" :key="s" :title="s" />
      </el-steps>
      <FieldTip content="推荐顺序：Prune 快照 → GC 孤儿块 → Compact 物理压缩。Dry run 不修改仓库。" />
      <div class="wizard-actions">
        <el-checkbox v-model="verifyAfter">完成后 Verify</el-checkbox>
        <el-button :loading="busy" :disabled="!repo.isOpen" @click="runWizardDryRun">
          Dry run 全流程
        </el-button>
        <el-button
          type="primary"
          :loading="busy"
          :disabled="!repo.isOpen"
          @click="runWizardLive"
        >
          执行重整
        </el-button>
      </div>
      <p v-if="lastWizardSummary" class="summary-line">{{ lastWizardSummary }}</p>
    </section>

    <section class="panel-card">
      <h2>物理压缩 (Compact)</h2>
      <el-switch v-model="compactDryRun" active-text="Dry run" />
      <p v-if="lastCompactSummary" class="summary-line">{{ lastCompactSummary }}</p>
      <el-button
        type="primary"
        class="action-btn"
        :loading="busy"
        :disabled="!repo.isOpen"
        @click="runCompact"
      >
        运行 Compact
      </el-button>
    </section>

    <section class="panel-card">
      <h2>孤儿块 GC</h2>
      <el-switch v-model="gcDryRun" active-text="Dry run" />
      <p v-if="lastGcSummary" class="summary-line">{{ lastGcSummary }}</p>
      <el-button
        type="primary"
        class="action-btn"
        :loading="busy"
        :disabled="!repo.isOpen"
        @click="runGc"
      >
        运行 GC
      </el-button>
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
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}
.head h2,
.panel-card h2 {
  margin: 0;
  font-size: 15px;
}
.head-actions {
  display: flex;
  gap: 8px;
  align-items: center;
}
.status-dl {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 6px 16px;
  margin: 10px 0 0;
  font-size: 13px;
}
.status-dl dt {
  color: var(--text-soft);
}
.status-dl dd {
  margin: 0;
}
.hint {
  margin: 8px 0 0;
  font-size: 12px;
  color: var(--text-soft);
}
.cards {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
  gap: 10px;
}
.stat-card {
  padding: 12px;
  border-radius: 8px;
  background: var(--hover-bg);
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.stat-card .label {
  font-size: 11px;
  color: var(--text-soft);
}
.stat-card .value {
  font-size: 16px;
  color: var(--text-main);
  font-weight: 600;
}
.wizard-card h2 {
  margin-bottom: 12px;
}
.wizard-actions {
  margin-top: 14px;
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  align-items: center;
}
.summary-line {
  margin: 10px 0 0;
  font-size: 12px;
  color: var(--text-soft);
}
.action-btn {
  margin-top: 12px;
}
.flow-line {
  margin: 8px 0;
  font-size: 12px;
  color: var(--text-soft);
  letter-spacing: 0.02em;
}
.explain-summary {
  display: flex;
  gap: 16px;
  font-size: 13px;
  margin: 10px 0;
}
.wizard-compare {
  font-size: 12px;
  color: var(--text-soft);
  margin-bottom: 8px;
}
.reason-bars {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-bottom: 12px;
}
.reason-row {
  display: grid;
  grid-template-columns: 72px 1fr 48px;
  gap: 8px;
  align-items: center;
  font-size: 12px;
}
.reason-label {
  color: var(--text-soft);
}
.reason-track {
  height: 8px;
  border-radius: 4px;
  background: var(--hover-bg);
  overflow: hidden;
}
.reason-fill {
  height: 100%;
  background: var(--accent, #409eff);
  border-radius: 4px;
  min-width: 2px;
}
.reason-count {
  text-align: right;
  color: var(--text-main);
}
.sample-table {
  margin-top: 8px;
}
.sample-table code {
  font-size: 11px;
}
</style>
