import { ElMessageBox } from "element-plus";

export async function confirmDestructive(
  title: string,
  message: string,
  confirmText = "确认执行"
): Promise<boolean> {
  try {
    await ElMessageBox.confirm(message, title, {
      type: "warning",
      confirmButtonText: confirmText,
      cancelButtonText: "取消",
      distinguishCancelAndClose: true,
    });
    return true;
  } catch {
    return false;
  }
}
