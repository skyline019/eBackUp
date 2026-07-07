import { lastError } from "@/api/ebbackup";

function stripCmdPrefix(msg: string): string {
  return msg.replace(/^(run_backup|run_restore|verify_repo|compact_repo|gc_orphans|prune_snapshots|recover_repo|init_repo|open_repo):\s*/i, "");
}

export function formatBackupError(e: unknown): string {
  const msg = stripCmdPrefix(e instanceof Error ? e.message : String(e));
  if (msg.includes("source path not found")) {
    return "源目录不存在，请点「浏览」重新选择有效文件夹";
  }
  if (msg.includes("prior manifest not found")) {
    return "尚无历史备份，首次请取消「增量备份」或改用全量模式";
  }
  return msg;
}

export function formatVerifyError(e: unknown): string {
  const msg = stripCmdPrefix(e instanceof Error ? e.message : String(e));
  if (msg.includes("EBBACKUP_AUDIT_KEY required for signature verify")) {
    return (
      "已开启「强制锚点验证」，但未设置审计密钥 EBBACKUP_AUDIT_KEY。" +
      "普通完整性验证请关闭该开关；若需验证 CARL 签名，请在启动 Workbench 前设置该环境变量。"
    );
  }
  if (msg.includes("no anchor published")) {
    return "仓库尚未发布 CARL 锚点。请先完成备份，或关闭「强制锚点验证」仅做 manifest/块校验。";
  }
  if (msg.includes("cannot open manifest")) {
    return "该快照或仓库尚无 manifest，请先完成至少一次备份。";
  }
  return msg;
}

export function formatRestoreError(e: unknown): string {
  const msg = stripCmdPrefix(e instanceof Error ? e.message : String(e));
  if (msg.includes("content key") || msg.includes("decrypt")) {
    return "解密失败：请确认已填写与备份时相同的密码。";
  }
  return msg;
}

export function formatGenericError(e: unknown, context?: string): string {
  const raw = stripCmdPrefix(e instanceof Error ? e.message : String(e));
  return context ? `${context}：${raw}` : raw;
}

/** 记录失败并尝试附加引擎 last_error 详情 */
export async function enrichError(e: unknown): Promise<string> {
  const base = e instanceof Error ? e.message : String(e);
  try {
    const detail = await lastError();
    if (detail && detail.trim() && !base.includes(detail.trim())) {
      return `${stripCmdPrefix(base)}（${detail.trim()}）`;
    }
  } catch {
    /* ignore */
  }
  return stripCmdPrefix(base);
}
