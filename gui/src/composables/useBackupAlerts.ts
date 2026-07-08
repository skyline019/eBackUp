import { ref, watch } from "vue";
import { ElNotification } from "element-plus";
import { rpoSummary, syncStatus, type RpoSummaryDto, type SyncStatusDto } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { isTauriRuntime } from "@/utils/tauriRuntime";
import { staleSyncMessage } from "@/utils/syncLabels";

const LS_PREFIX = "ebbackup_stale_toast_";
const LS_SYNC_PREFIX = "ebbackup_stale_sync_toast_";
const TOAST_COOLDOWN_MS = 24 * 60 * 60 * 1000;

export const rpoSummaryData = ref<RpoSummaryDto | null>(null);
export const rpoLoading = ref(false);
export const staleAlertVisible = ref(false);

export const syncStatusData = ref<SyncStatusDto | null>(null);
export const syncStatusLoading = ref(false);
export const staleSyncAlertVisible = ref(false);

function toastKey(repoPath: string, days: number): string {
  return `${LS_PREFIX}${repoPath}::${Math.floor(days)}`;
}

function syncToastKey(repoPath: string, lag: number): string {
  return `${LS_SYNC_PREFIX}${repoPath}::${lag}`;
}

function shouldShowToast(key: string): boolean {
  try {
    const raw = localStorage.getItem(key);
    if (!raw) return true;
    const last = Number(raw);
    return !Number.isFinite(last) || Date.now() - last >= TOAST_COOLDOWN_MS;
  } catch {
    return true;
  }
}

function markToastShown(key: string) {
  try {
    localStorage.setItem(key, String(Date.now()));
  } catch {
    /* ignore */
  }
}

export async function refreshRpoSummary() {
  const repo = useRepoStore();
  if (!isTauriRuntime() || !repo.isOpen) {
    rpoSummaryData.value = null;
    staleAlertVisible.value = false;
    return;
  }
  rpoLoading.value = true;
  try {
    const summary = await rpoSummary();
    rpoSummaryData.value = summary;
    const ui = useUiStore();
    const threshold = ui.settings.staleBackupAlertDays ?? 7;
    const days = summary.days_since_last_success;
    const stale =
      summary.snapshot_count === 0 ||
      (days >= 0 && days > threshold);
    staleAlertVisible.value = stale;
    if (stale && repo.path && shouldShowToast(toastKey(repo.path, days))) {
      const msg =
        summary.snapshot_count === 0
          ? "仓库尚无成功备份快照"
          : `距上次成功备份已 ${days.toFixed(1)} 天（阈值 ${threshold} 天）`;
      ElNotification.warning({
        title: "备份滞后告警",
        message: msg,
        duration: 8000,
      });
      markToastShown(toastKey(repo.path, days));
    }
  } catch {
    rpoSummaryData.value = null;
    staleAlertVisible.value = false;
  } finally {
    rpoLoading.value = false;
  }
}

export async function refreshSyncStatus() {
  const repo = useRepoStore();
  if (!isTauriRuntime() || !repo.isOpen) {
    syncStatusData.value = null;
    staleSyncAlertVisible.value = false;
    return;
  }
  syncStatusLoading.value = true;
  try {
    const status = await syncStatus();
    syncStatusData.value = status;
    const ui = useUiStore();
    const threshold = ui.settings.staleSyncAlertDays ?? 3;
    const lag = status.remote_lag_txn ?? 0;
    let daysSinceSync = -1;
    if (status.last_success_unix > 0) {
      daysSinceSync = (Date.now() / 1000 - status.last_success_unix) / 86400;
    }
    const stale =
      lag > 0 ||
      (status.remote_type === "ferry" &&
        status.last_ferry_target_txn === 0 &&
        status.latest_txn > 0) ||
      (status.remote_type !== "ferry" &&
        status.last_success_unix === 0 &&
        status.latest_txn > 0) ||
      (daysSinceSync >= 0 && daysSinceSync > threshold && lag > 0);
    staleSyncAlertVisible.value = stale;
    if (stale && repo.path) {
      const key = syncToastKey(repo.path, lag);
      if (shouldShowToast(key)) {
        ElNotification.warning({
          title: "同步滞后告警",
          message: staleSyncMessage(status),
          duration: 8000,
        });
        markToastShown(key);
      }
    }
  } catch {
    syncStatusData.value = null;
    staleSyncAlertVisible.value = false;
  } finally {
    syncStatusLoading.value = false;
  }
}

export function useBackupAlerts() {
  const repo = useRepoStore();
  const ui = useUiStore();

  watch(
    () =>
      [repo.isOpen, repo.path, ui.activity, ui.settings.staleBackupAlertDays] as const,
    () => {
      void refreshRpoSummary();
    },
    { immediate: true }
  );

  watch(
    () =>
      [repo.isOpen, repo.path, ui.activity, ui.settings.staleSyncAlertDays] as const,
    () => {
      void refreshSyncStatus();
    },
    { immediate: true }
  );
}
