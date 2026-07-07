type Runner = () => void | Promise<void>;

const runners = new Map<string, Runner>();

/** 活动页注册可远程触发的操作（菜单/快捷键） */
export function setActivityRunner(id: string, fn: Runner): () => void {
  runners.set(id, fn);
  return () => runners.delete(id);
}

export async function invokeActivityRunner(id: string): Promise<boolean> {
  const fn = runners.get(id);
  if (!fn) return false;
  await fn();
  return true;
}
