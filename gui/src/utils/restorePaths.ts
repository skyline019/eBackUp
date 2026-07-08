export type RestoreLayoutMode = "keep" | "strip_prefix" | "flatten" | "remap_prefix";
export type RestoreConflictPolicy = "fail" | "skip" | "suffix";
export type AclRestorePolicy = "inherit" | "preserve" | "skip" | "best_effort";
export type ReparseRestorePolicy = "skip" | "recreate";

export interface RestoreRemapDto {
  mode: RestoreLayoutMode;
  strip_prefix?: string;
  map_from?: string;
  map_to?: string;
  conflict?: RestoreConflictPolicy;
  acl_policy?: AclRestorePolicy;
  reparse_policy?: ReparseRestorePolicy;
}

function normalizePath(p: string): string {
  return p.replace(/\\/g, "/");
}

function startsWithPath(text: string, prefix: string): boolean {
  if (!prefix) return true;
  if (text.length < prefix.length) return false;
  if (text.slice(0, prefix.length) !== prefix) return false;
  if (text.length === prefix.length) return true;
  const next = text[prefix.length];
  return next === "/";
}

/** Minimal cover set for include_path filter generation. */
export function collapseIncludePaths(paths: string[]): string[] {
  const unique = [...new Set(paths.map(normalizePath).filter(Boolean))];
  unique.sort((a, b) => {
    const depth = (s: string) => (s.match(/\//g)?.length ?? 0) + 1;
    const d = depth(a) - depth(b);
    return d !== 0 ? d : a.localeCompare(b);
  });
  const result: string[] = [];
  for (const p of unique) {
    if (result.some((q) => startsWithPath(p, q))) continue;
    for (let i = result.length - 1; i >= 0; i--) {
      if (startsWithPath(result[i], p)) result.splice(i, 1);
    }
    result.push(p);
  }
  return result;
}

export function buildFilterJson(includePaths: string[]): string {
  return JSON.stringify({ include_paths: collapseIncludePaths(includePaths) });
}

export function buildRemapJson(remap: RestoreRemapDto): string {
  const body: Record<string, string> = { mode: remap.mode };
  if (remap.strip_prefix) body.strip_prefix = remap.strip_prefix;
  if (remap.map_from) body.map_from = remap.map_from;
  if (remap.map_to) body.map_to = remap.map_to;
  if (remap.conflict) body.conflict = remap.conflict;
  if (remap.acl_policy) body.acl_policy = remap.acl_policy;
  if (remap.reparse_policy) body.reparse_policy = remap.reparse_policy;
  return JSON.stringify(body);
}

export function previewSubsetLocally(
  files: { relative_path: string; size: number; file_type: string }[],
  includePaths: string[]
): { fileCount: number; dirCount: number; totalBytes: number } {
  const collapsed = collapseIncludePaths(includePaths);
  let fileCount = 0;
  let dirCount = 0;
  let totalBytes = 0;
  for (const f of files) {
    const rel = normalizePath(f.relative_path);
    if (!collapsed.some((p) => startsWithPath(rel, p))) continue;
    if (f.file_type === "dir") {
      dirCount++;
    } else if (f.file_type === "file") {
      fileCount++;
      totalBytes += f.size;
    }
  }
  return { fileCount, dirCount, totalBytes };
}
