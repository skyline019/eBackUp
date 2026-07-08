import { ref, watch } from "vue";
import { ElNotification } from "element-plus";
import { rpoSummary, type RpoSummaryDto } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { isTauriRuntime } from "@/utils/tauriRuntime";

const LS_PREFIX = "ebbackup_stale_toast_";
const TOAST_COOLDOWN_MS = 24 * 60 * 60 * 1000;

export const rpoSummaryData = ref<RpoSummaryDto | null>(null);
export const rpoLoading = ref(false);
export const staleAlertVisible = ref(false);

function toastKey(repoPath: string, days: number): string {
  return `${LS_PREFIX}${repoPath}::${Math.floor(days)}`;
}

function shouldShowToast(repoPath: string, days: number): boolean {
  try {
    const raw = localStorage.getItem(toastKey(repoPath, days));
    if (!raw) return true;
    const last = Number(raw);
    return !Number.isFinite(last) || Date.now() - last >= TOAST_COOLDOWN_MS;
  } catch {
    return true;
  }
}

function markToastShown(repoPath: string, days: number) {
  try {
    localStorage.setItem(toastKey(repoPath, days), String(Date.now()));
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
    if (stale && repo.path && shouldShowToast(repo.path, days)) {
      const msg =
        summary.snapshot_count === 0
          ? "仓库尚无成功备份快照"
          : `距上次成功备份已 ${days.toFixed(1)} 天（阈值 ${threshold} 天）`;
      ElNotification.warning({
        title: "备份滞后告警",
        message: msg,
        duration: 8000,
      });
      markToastShown(repo.path, days);
    }
  } catch {
    rpoSummaryData.value = null;
    staleAlertVisible.value = false;
  } finally {
    rpoLoading.value = false;
  }
}

export function useBackupAlerts() {
  const repo = useRepoStore();
  const ui = useUiStore();

  watch(
    () => [repo.isOpen, repo.path, ui.activity, ui.settings.staleBackupAlertDays] as const,
    () => {
      void refreshRpoSummary();
    },
    { immediate: true }
  );
}
