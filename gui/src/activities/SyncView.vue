<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import {
  exportDelta,
  getProfileState,
  importDelta,
  pickDirectory,
  pickFile,
  setProfileState,
  syncFerryExport,
  syncInitFerry,
  syncInitLocal,
  syncInitPds,
  syncPdsAuth,
  syncPdsAuthUrl,
  syncPush,
  type SyncStatusDto,
} from "@/api/ebbackup";
import { useProfileStore } from "@/stores/profileStore";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import {
  refreshSyncStatus,
  syncStatusData,
  syncStatusLoading,
} from "@/composables/useBackupAlerts";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import {
  formatTransportLabel,
  maintenanceBlockLabel,
  syncLagLabel,
} from "@/utils/syncLabels";
import FieldTip from "@/components/FieldTip.vue";

const repo = useRepoStore();
const profile = useProfileStore();
const ui = useUiStore();

const busy = ref(false);
const syncMode = ref<"ferry" | "local_mirror" | "s3" | "pds">("ferry");
const mirrorPath = ref("");
const pdsDomain = ref("");
const pdsCredentialsPath = ref("");
const pdsAuthCode = ref("");
const pdsAuthUrl = ref("");
const ferryOutDir = ref("");
const ferryStep = ref(1);
const showImportSection = ref(false);
const deltaBaseTxn = ref<number | undefined>(undefined);
const deltaTargetTxn = ref<number | undefined>(undefined);
const importBasePath = ref("");
const importDeltaPath = ref("");
const importOutRepo = ref("");

const status = computed(() => syncStatusData.value as SyncStatusDto | null);
const transportLabel = computed(() =>
  status.value
    ? formatTransportLabel(status.value.transport, status.value.remote_type)
    : "—"
);
const lagTitle = computed(() => (status.value ? syncLagLabel(status.value) : ""));
const maintenanceTitle = computed(() => maintenanceBlockLabel(status.value));

onMounted(async () => {
  if (repo.isOpen) {
    await loadProfileSyncPrefs();
    await refresh();
  }
});

watch(
  () => repo.path,
  async (path) => {
    if (!path) return;
    await loadProfileSyncPrefs();
  }
);

async function loadProfileSyncPrefs() {
  if (!repo.path || !profile.activeProfileId) return;
  try {
    const state = await getProfileState(profile.activeProfileId);
    ferryOutDir.value = state.syncFerryOutDirs?.[repo.path] ?? ferryOutDir.value;
    mirrorPath.value = state.syncMirrorDirs?.[repo.path] ?? mirrorPath.value;
  } catch {
    /* optional */
  }
}

async function persistFerryDir() {
  if (!repo.path || !profile.activeProfileId || !ferryOutDir.value.trim()) return;
  try {
    const state = await getProfileState(profile.activeProfileId);
    await setProfileState(profile.activeProfileId, {
      ...state,
      syncFerryOutDirs: {
        ...(state.syncFerryOutDirs ?? {}),
        [repo.path]: ferryOutDir.value.trim(),
      },
    });
  } catch {
    /* optional */
  }
}

async function persistMirrorDir() {
  if (!repo.path || !profile.activeProfileId || !mirrorPath.value.trim()) return;
  try {
    const state = await getProfileState(profile.activeProfileId);
    await setProfileState(profile.activeProfileId, {
      ...state,
      syncMirrorDirs: {
        ...(state.syncMirrorDirs ?? {}),
        [repo.path]: mirrorPath.value.trim(),
      },
    });
  } catch {
    /* optional */
  }
}

async function refresh() {
  await refreshSyncStatus();
  const st = syncStatusData.value;
  if (st?.remote_type === "ferry") syncMode.value = "ferry";
  else if (st?.remote_type === "local_mirror") syncMode.value = "local_mirror";
  else if (st?.remote_type === "s3") syncMode.value = "s3";
  else if (st?.remote_type === "pds") syncMode.value = "pds";
  if (st?.pds_domain_id) pdsDomain.value = st.pds_domain_id;
  if (st?.local_mirror_root) mirrorPath.value = st.local_mirror_root;
  if (repo.snapshots.length) {
    const latest = repo.snapshots[repo.snapshots.length - 1]?.txn_id;
    if (latest && !deltaTargetTxn.value) deltaTargetTxn.value = latest;
    const base = st?.last_export_base_txn || st?.synced_txn;
    if (base && !deltaBaseTxn.value) deltaBaseTxn.value = base;
  }
}

async function applySyncMode() {
  if (!repo.isOpen) return;
  busy.value = true;
  try {
    if (syncMode.value === "ferry") {
      const res = await syncInitFerry();
      ui.pushLog(res.message || "已启用摆渡模式", "success");
    } else if (syncMode.value === "local_mirror") {
      if (!mirrorPath.value.trim()) {
        ui.pushLog("请选择本地镜像目录", "error");
        return;
      }
      const res = await syncInitLocal(mirrorPath.value.trim());
      await persistMirrorDir();
      ui.pushLog(res.message || "已配置本地镜像", "success");
    } else if (syncMode.value === "pds") {
      if (!pdsDomain.value.trim() || !pdsCredentialsPath.value.trim()) {
        ui.pushLog("请填写 PDS 域 ID 并选择 RAM OAuth 凭证 CSV", "error");
        return;
      }
      const res = await syncInitPds(
        pdsDomain.value.trim(),
        pdsCredentialsPath.value.trim()
      );
      ui.pushLog(res.message || "已配置 PDS", "success");
    }
    await refresh();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "同步模式配置失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function runPush() {
  if (!repo.isOpen) return;
  busy.value = true;
  try {
    const res = await syncPush(true);
    ui.pushLog(res.message || "Push 完成", "success");
    await refresh();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "Push 失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function browseMirror() {
  const p = await pickDirectory();
  if (p) mirrorPath.value = p;
}

async function browsePdsCredentials() {
  const p = await pickFile();
  if (p) pdsCredentialsPath.value = p;
}

async function openPdsAuth() {
  if (!repo.isOpen) return;
  busy.value = true;
  try {
    const res = await syncPdsAuthUrl();
    pdsAuthUrl.value = res.url;
    await navigator.clipboard.writeText(res.url);
    ui.pushLog("授权 URL 已复制到剪贴板，请在浏览器完成登录后粘贴授权码", "success");
    window.open(res.url, "_blank");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "获取授权 URL 失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function submitPdsAuth() {
  if (!repo.isOpen || !pdsAuthCode.value.trim()) {
    ui.pushLog("请粘贴 OAuth 授权码", "error");
    return;
  }
  busy.value = true;
  try {
    const res = await syncPdsAuth(pdsAuthCode.value.trim());
    ui.pushLog(res.message || "PDS 授权成功", "success");
    pdsAuthCode.value = "";
    await refresh();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "PDS 授权失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function browseFerryOut() {
  const p = await pickDirectory();
  if (p) {
    ferryOutDir.value = p;
    await persistFerryDir();
  }
}

async function runFerryExport() {
  if (!repo.isOpen || !ferryOutDir.value.trim()) {
    ui.pushLog("请指定摆渡输出目录", "error");
    return;
  }
  busy.value = true;
  try {
    await persistFerryDir();
    const res = await syncFerryExport(ferryOutDir.value.trim(), {
      autoBase: deltaBaseTxn.value == null,
      baseTxnId: deltaBaseTxn.value,
      targetTxnId: deltaTargetTxn.value,
    });
    ui.pushLog(res.message || "Delta 摆渡包已导出", "success");
    ferryStep.value = 3;
    showImportSection.value = true;
    await refresh();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "摆渡导出失败"), "error");
  } finally {
    busy.value = false;
  }
}

async function runGuiDeltaExport() {
  if (!deltaBaseTxn.value || !ferryOutDir.value.trim()) {
    ui.pushLog("需要 base txn 与输出路径（.ebb 文件）", "error");
    return;
  }
  const bundlePath =
    ferryOutDir.value.endsWith(".ebb") || ferryOutDir.value.endsWith(".EBB")
      ? ferryOutDir.value
      : `${ferryOutDir.value.replace(/[\\/]+$/, "")}\\delta_${deltaBaseTxn.value}_${deltaTargetTxn.value ?? "latest"}.ebb`;
  busy.value = true;
  try {
    await exportDelta(deltaBaseTxn.value, bundlePath, deltaTargetTxn.value, undefined);
    ui.pushLog(`已导出 Delta：${bundlePath}`, "success");
    ferryStep.value = 3;
    showImportSection.value = true;
    await refresh();
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e)), "error");
  } finally {
    busy.value = false;
  }
}

async function browseImportBase() {
  const p = await pickFile();
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
  if (!importBasePath.value || !importDeltaPath.value || !importOutRepo.value) {
    ui.pushLog("请填写 base、delta 与目标仓库路径", "error");
    return;
  }
  busy.value = true;
  try {
    await importDelta(importBasePath.value, importDeltaPath.value, importOutRepo.value);
    ui.pushLog("Delta 导入完成", "success");
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e)), "error");
  } finally {
    busy.value = false;
  }
}

function formatTime(unix: number) {
  if (!unix) return "—";
  return new Date(unix * 1000).toLocaleString();
}
</script>

<template>
  <div class="activity-page sync-view">
    <section class="panel-card">
      <h2>同步方式（无云推荐）</h2>
      <FieldTip content="无云仓库时优先使用「摆渡」或「本地镜像」；PDS / S3 可稍后配置。" />
      <el-form label-width="120px" class="mode-form">
        <el-form-item label="模式">
          <el-radio-group v-model="syncMode">
            <el-radio value="ferry">摆渡（推荐）</el-radio>
            <el-radio value="local_mirror">本地镜像</el-radio>
            <el-radio value="pds">阿里云 PDS</el-radio>
            <el-radio value="s3">S3（稍后）</el-radio>
          </el-radio-group>
        </el-form-item>
        <el-form-item v-if="syncMode === 'local_mirror'" label="镜像目录">
          <div class="row">
            <el-input v-model="mirrorPath" placeholder="D:\sync-mirror 或 \\nas\share\mirror" />
            <el-button @click="browseMirror">浏览</el-button>
          </div>
        </el-form-item>
        <template v-if="syncMode === 'pds'">
          <el-form-item label="域 ID">
            <el-input v-model="pdsDomain" placeholder="例如 bj36449" />
          </el-form-item>
          <el-form-item label="OAuth 凭证">
            <div class="row">
              <el-input v-model="pdsCredentialsPath" placeholder="RAM 应用 appSecret.csv 路径" />
              <el-button @click="browsePdsCredentials">选择 CSV</el-button>
            </div>
          </el-form-item>
        </template>
        <el-form-item>
          <el-button type="primary" :loading="busy" @click="applySyncMode">保存同步配置</el-button>
        </el-form-item>
      </el-form>
    </section>

    <section class="panel-card">
      <div class="head-row">
        <h2>同步状态</h2>
        <el-button link type="primary" :loading="syncStatusLoading" @click="refresh">刷新</el-button>
      </div>
      <p v-if="!repo.isOpen" class="hint">请先打开仓库。</p>
      <template v-else-if="status">
        <el-alert
          v-if="lagTitle"
          type="warning"
          show-icon
          :closable="false"
          class="lag-alert"
          :title="lagTitle"
        />
        <el-alert
          v-if="maintenanceTitle"
          type="error"
          show-icon
          :closable="false"
          class="lag-alert"
          :title="maintenanceTitle"
        />
        <dl class="status-dl">
          <dt>模式</dt><dd>{{ status.sync_mode_label || transportLabel }}</dd>
          <dt>传输</dt><dd>{{ transportLabel }}</dd>
          <dt>最新 txn</dt><dd>{{ status.latest_txn }}</dd>
          <dt>已同步 txn</dt><dd>{{ status.synced_txn }}</dd>
          <dt>已摆渡 txn</dt><dd>{{ status.last_ferry_target_txn || "—" }}</dd>
          <dt>待同步 txn</dt><dd>{{ status.pending_txn || "—" }}</dd>
          <dt>待传 chunk</dt><dd>{{ status.pending_chunk_count }}</dd>
          <dt>镜像路径</dt><dd>{{ status.local_mirror_root || "—" }}</dd>
          <template v-if="status.remote_type === 'pds'">
            <dt>PDS 域</dt><dd>{{ status.pds_domain_id || "—" }}</dd>
            <dt>网盘 drive</dt><dd>{{ status.pds_drive_id || "—" }}</dd>
            <dt>已授权</dt><dd>{{ status.pds_authed ? "是" : "否" }}</dd>
          </template>
          <dt>上次导出 base</dt><dd>{{ status.last_export_base_txn || "—" }}</dd>
          <dt>上次同步</dt><dd>{{ formatTime(status.last_success_unix) }}</dd>
          <dt v-if="status.last_error">最近错误</dt>
          <dd v-if="status.last_error" class="err">{{ status.last_error }}</dd>
        </dl>
        <div v-if="syncMode !== 'ferry'" class="actions">
          <el-button type="primary" :loading="busy" @click="runPush">立即 Push</el-button>
        </div>
        <div v-if="syncMode === 'pds'" class="pds-auth">
          <el-button :loading="busy" @click="openPdsAuth">获取授权 URL</el-button>
          <el-input
            v-model="pdsAuthCode"
            placeholder="浏览器回调后粘贴授权码 code"
            class="auth-code"
          />
          <el-button type="primary" :loading="busy" @click="submitPdsAuth">完成授权</el-button>
          <p v-if="pdsAuthUrl" class="hint auth-url">{{ pdsAuthUrl }}</p>
        </div>
      </template>
      <p v-else class="hint">无法读取同步状态；请确认已构建 eb-sync 并运行 npm run sync:runtime。</p>
    </section>

    <section class="panel-card">
      <h2>轨道 A — Delta 摆渡向导</h2>
      <el-steps :active="ferryStep - 1" finish-status="success" class="ferry-steps">
        <el-step title="选择目录" />
        <el-step title="导出 Delta" />
        <el-step title="对端导入" />
      </el-steps>
      <FieldTip content="完全断网时：导出 delta.ebb → 物理搬运（U 盘/NAS）→ 对端导入。" />
      <el-form label-width="120px" class="ferry-form">
        <el-form-item label="① 输出目录">
          <div class="row">
            <el-input v-model="ferryOutDir" placeholder="D:\ferry-drop 或 \\nas\share" @blur="persistFerryDir" />
            <el-button @click="browseFerryOut">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="② Base txn">
          <el-input-number v-model="deltaBaseTxn" :min="0" :step="1" controls-position="right" />
        </el-form-item>
        <el-form-item label="② Target txn">
          <el-input-number v-model="deltaTargetTxn" :min="0" :step="1" controls-position="right" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="busy" @click="runFerryExport">eb-sync 摆渡导出</el-button>
          <el-button :loading="busy" @click="runGuiDeltaExport">引擎 Delta 导出</el-button>
        </el-form-item>
        <el-form-item label="③ 对端导入">
          <el-button link type="primary" @click="showImportSection = !showImportSection">
            {{ showImportSection ? "收起导入表单" : "展开导入表单" }}
          </el-button>
        </el-form-item>
      </el-form>
    </section>

    <section v-show="showImportSection" class="panel-card">
      <h2>导入 Delta（对端）</h2>
      <el-form label-width="120px">
        <el-form-item label="Base（仓库或 .ebb）">
          <div class="row">
            <el-input v-model="importBasePath" />
            <el-button @click="browseImportBase">文件</el-button>
          </div>
        </el-form-item>
        <el-form-item label="Delta .ebb">
          <div class="row">
            <el-input v-model="importDeltaPath" />
            <el-button @click="browseImportDelta">文件</el-button>
          </div>
        </el-form-item>
        <el-form-item label="输出仓库">
          <div class="row">
            <el-input v-model="importOutRepo" />
            <el-button @click="browseImportOut">目录</el-button>
          </div>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="busy" @click="runImportDelta">导入</el-button>
        </el-form-item>
      </el-form>
    </section>
  </div>
</template>

<style scoped>
.sync-view {
  display: flex;
  flex-direction: column;
  gap: 16px;
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
.head-row h2,
.panel-card h2 {
  margin: 0;
  font-size: 15px;
}
.status-dl {
  display: grid;
  grid-template-columns: 140px 1fr;
  gap: 8px 12px;
  font-size: 13px;
  margin: 12px 0;
}
.status-dl dt {
  color: var(--text-soft);
}
.status-dl dd {
  margin: 0;
  word-break: break-all;
}
.status-dl dd.err {
  color: var(--el-color-danger);
}
.lag-alert {
  margin-bottom: 12px;
}
.actions {
  margin-top: 8px;
}
.pds-auth {
  margin-top: 16px;
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  align-items: center;
}
.pds-auth .auth-code {
  flex: 1;
  min-width: 200px;
}
.pds-auth .auth-url {
  width: 100%;
  word-break: break-all;
  font-size: 12px;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
.hint {
  color: var(--text-soft);
  font-size: 13px;
}
.ferry-form,
.mode-form {
  margin-top: 12px;
}
.ferry-steps {
  margin: 12px 0 16px;
}
</style>
