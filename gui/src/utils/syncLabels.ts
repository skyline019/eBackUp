import type { SyncStatusDto } from "@/api/ebbackup";

export function formatTransportLabel(transport: string, remoteType?: string): string {
  switch (transport) {
    case "local_mirror":
      return "本地镜像";
    case "local_dir":
      return "本地镜像（环境变量）";
    case "local_fallback":
      return "本地镜像（仓库内 .sync_remote）";
    case "ferry":
      return "Delta 摆渡";
    case "s3":
      return "S3 在线";
    case "pds":
      return "阿里云 PDS";
    default:
      if (remoteType === "ferry") return "Delta 摆渡";
      if (remoteType === "local_mirror") return "本地镜像";
      if (remoteType === "pds") return "阿里云 PDS";
      return transport || "未配置";
  }
}

export function syncLagLabel(status: SyncStatusDto): string {
  const lag = status.remote_lag_txn ?? 0;
  if (lag <= 0) return "";
  if (status.remote_type === "ferry") {
    return `待导出 ${lag} 个 txn（本地 ${status.latest_txn} / 已摆渡 ${status.last_ferry_target_txn ?? 0}）`;
  }
  return `待同步 ${lag} 个 txn（本地 ${status.latest_txn} / 已同步 ${status.synced_txn}）`;
}

export function maintenanceBlockLabel(status: SyncStatusDto | null): string {
  if (!status?.maintenance_blocked) return "";
  if (status.remote_type === "ferry") {
    return "维护前请先完成 Delta 摆渡导出（compact/GC 可能删除未导出 chunk）";
  }
  if (status.remote_type === "local_mirror" || status.transport === "local_fallback") {
    return "维护前请先完成本地镜像 Push（compact/GC 可能删除未上传 chunk）";
  }
  return "维护前请先完成同步（compact/GC 可能删除未上传 chunk）";
}

export function staleSyncMessage(status: SyncStatusDto): string {
  const lag = status.remote_lag_txn ?? 0;
  if (status.remote_type === "ferry") {
    return lag > 0 ? `有 ${lag} 个 txn 尚未摆渡导出` : "尚未完成过 Delta 摆渡导出";
  }
  if (lag > 0) return `有 ${lag} 个 txn 尚未同步到镜像/远端`;
  return "尚未完成过同步";
}
