export type ActivityId =
  | "repo"
  | "backup"
  | "snapshots"
  | "restore"
  | "verify"
  | "maintenance";

export const ACTIVITIES: { id: ActivityId; label: string; hint: string }[] = [
  { id: "repo", label: "仓库", hint: "初始化 / 打开备份仓库 (Ctrl+1)" },
  { id: "backup", label: "备份", hint: "运行增量备份 (Ctrl+2)" },
  { id: "snapshots", label: "快照", hint: "快照列表与保留策略 (Ctrl+3)" },
  { id: "restore", label: "恢复", hint: "还原到目标目录 (Ctrl+4)" },
  { id: "verify", label: "验证", hint: "完整性验证与修复 (Ctrl+5)" },
  { id: "maintenance", label: "维护", hint: "统计 / 压缩 / GC (Ctrl+6)" },
];
