import { invoke } from "@tauri-apps/api/core";
import { ElMessage } from "element-plus";
import { isTauriRuntime } from "@/utils/tauriRuntime";

export class TauriNotAvailableError extends Error {
  constructor() {
    super("请在 Tauri 桌面模式中运行（npm run tauri:dev），浏览器预览无法调用原生 API");
    this.name = "TauriNotAvailableError";
  }
}

export function requireTauri(): void {
  if (!isTauriRuntime()) {
    throw new TauriNotAvailableError();
  }
}

export async function invokeSafe<T>(
  cmd: string,
  args?: Record<string, unknown>,
  opts?: { silent?: boolean }
): Promise<T> {
  requireTauri();
  try {
    return await invoke<T>(cmd, args);
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    if (!opts?.silent) {
      ElMessage.error(`${cmd}: ${msg}`);
    }
    throw e instanceof Error ? e : new Error(msg);
  }
}
