export type TaskKind =
  | "backup"
  | "restore"
  | "verify"
  | "recover"
  | "compact"
  | "gc"
  | "prune";

export interface TaskPhase {
  id: string;
  label: string;
  /** permille upper bound (exclusive), backup engine mapping */
  until?: number;
}

export const TASK_LABELS: Record<TaskKind, string> = {
  backup: "备份",
  restore: "恢复",
  verify: "验证",
  recover: "修复事务",
  compact: "Compact",
  gc: "孤儿块 GC",
  prune: "快照 Prune",
};

export const BACKUP_PHASES: TaskPhase[] = [
  { id: "scan", label: "扫描源目录", until: 50 },
  { id: "chunk", label: "CDC 分块 + 摘要", until: 400 },
  { id: "store", label: "压缩存储 + Pipeline", until: 960 },
  { id: "commit", label: "Manifest 提交点", until: 1001 },
];

export const RESTORE_PHASES: TaskPhase[] = [
  { id: "load", label: "加载快照清单" },
  { id: "write", label: "还原文件" },
  { id: "finish", label: "完成" },
];

export const VERIFY_PHASES: TaskPhase[] = [
  { id: "manifest", label: "校验 Manifest" },
  { id: "chunks", label: "深度块校验" },
  { id: "anchor", label: "审计锚点（可选）" },
];

export const MAINTENANCE_PHASES: TaskPhase[] = [
  { id: "analyze", label: "分析仓库" },
  { id: "apply", label: "应用变更" },
];

export function phasesForKind(kind: TaskKind): TaskPhase[] {
  switch (kind) {
    case "backup":
      return BACKUP_PHASES;
    case "restore":
      return RESTORE_PHASES;
    case "verify":
    case "recover":
      return VERIFY_PHASES;
    case "compact":
    case "gc":
    case "prune":
      return MAINTENANCE_PHASES;
    default:
      return [];
  }
}

export function phaseIndexForPermille(phases: TaskPhase[], permille: number): number {
  if (!phases.length) return 0;
  for (let i = 0; i < phases.length; i++) {
    const until = phases[i].until ?? 1001;
    if (permille < until) return i;
  }
  return phases.length - 1;
}

export function percentFromPermille(permille: number): number {
  return Math.min(100, Math.max(0, permille / 10));
}
