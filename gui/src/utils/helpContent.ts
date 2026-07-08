import type { ActivityId } from "@/utils/activities";

export interface HelpSection {
  id: string;
  title: string;
  body: string[];
}

export const HELP_QUICKSTART: HelpSection[] = [
  {
    id: "flow",
    title: "三分钟答辩演示",
    body: [
      "1. 仓库页：创建或打开本地 repo（空仓库统计为零属正常）。",
      "2. 备份页：选择真实存在的源目录 → 运行备份（首次自动全量）。",
      "3. 快照页：刷新查看 txn 列表；「内容」页可查询已备份文件。",
      "4. 恢复页：选目标目录与快照 → 开始恢复。",
      "5. 验证页：保持「强制锚点」关闭 → 验证仓库。",
    ],
  },
  {
    id: "desktop",
    title: "运行环境",
    body: [
      "完整功能需桌面版：npm run tauri:dev 或安装 release 包。",
      "浏览器 Vite 预览仅可调整外观，无法调用备份引擎。",
      "DLL 与 eb 命令行共用同一 C++ 内核（ABI v29）。",
    ],
  },
];

export const HELP_FAQ: HelpSection[] = [
  {
    id: "source",
    title: "备份报 source path not found",
    body: ["源路径在磁盘上不存在。请用「浏览」重新选择文件夹，勿手输错误路径。"],
  },
  {
    id: "incremental",
    title: "增量备份失败 / prior manifest",
    body: ["空仓库或尚无 manifest 时只能全量。取消「增量备份」或让系统自动切换全量。"],
  },
  {
    id: "unicode-path",
    title: "中文或含空格的路径",
    body: [
      "源目录、仓库目录、恢复目标均支持中文与空格（UTF-8）。",
      "请用「浏览」选择路径，避免从资源管理器复制后乱码。",
      "与 CLI 共用引擎：路径字符串统一为 UTF-8。",
    ],
  },
  {
    id: "audit-key",
    title: "审计密钥是什么？",
    body: [
      "CARL 锚点签名的密钥，在验证页「审计密钥」栏直接填写即可。",
      "课程演示：关闭「强制锚点」，仅做 manifest/chunk 校验。",
      "密钥仅保存在当前引擎会话，不写入设置文件。",
    ],
  },
  {
    id: "encrypt",
    title: "加密备份与恢复",
    body: [
      "备份页勾选「加密」并设置密码；恢复页须填写相同密码。",
      "密码通过引擎会话传递，不会写入设置文件。",
    ],
  },
  {
    id: "filter",
    title: "过滤器文件",
    body: [
      "可选 .filter 规则文件，语法与 CLI 一致（路径/glob/尺寸/时间）。",
      "备份与恢复均可加载同一过滤器以限定文件集。",
    ],
  },
  {
    id: "smart-exclude",
    title: "智能排除建议",
    body: [
      "备份页「分析源目录」对源做浅层扫描，列出 .git、node_modules、Thumbs.db 等可解释建议。",
      "建议不会自动生效；点击「全部采纳」写入作业的 exclude_paths / exclude_globs，或 ad-hoc 备份时写入会话 filter。",
      "勾选「包含 IDE 目录」可建议 .idea / .vscode；catalog 亦覆盖 .next、.turbo、bin/obj、coverage、*.log 等。",
      "CLI 等价：eb suggest-excludes <source> [--json] [--include-ide]；已有 filter 会抑制重复建议。",
    ],
  },
  {
    id: "backup-window",
    title: "备份窗口与 durability 自适应",
    body: [
      "作业可配置 window_start / window_end（HH:MM 本地时区）；仅在窗口内 RunJob/队列执行，窗外跳过。",
      "勾选 durability_adaptive 后，距 window_end 不足 grace 秒（默认 300）时自动 Strict→Balanced。",
      "超时未完成会截断为部分快照；报告含 durability_downgraded / window_truncated。",
    ],
  },
  {
    id: "transparency",
    title: "壁纸模式透明度",
    body: [
      "设置 → 壁纸：上传图片/视频后，工作区右上角「卡片」与输出区「输出」滑块调节透明度。",
      "若看起来不透明，确认已启用壁纸且滑块低于 100%。",
    ],
  },
  {
    id: "stale-backup",
    title: "备份滞后本地告警",
    body: [
      "打开仓库后自动拉取 RPO 摘要；若距上次成功备份超过设置天数（默认 7），弹出通知并在仓库页显示警告。",
      "同一仓库同一滞后级别 24 小时内不重复 toast。",
      "告警阈值按 Profile 保存；切换 Profile 后各自独立。",
    ],
  },
  {
    id: "profile",
    title: "多 Profile 是什么？",
    body: [
      "Ribbon 左侧可切换 Profile：每个 Profile 独立保存主题/壁纸、最近仓库、上次源路径与备份滞后告警天数。",
      "切换时会关闭当前仓库并加载该 Profile 的设置；若 Profile 记录了上次打开的仓库会自动尝试重开。",
      "设置 → Profile 页可重命名或删除（default 不可删；当前激活项不可删）。",
    ],
  },
  {
    id: "plugins",
    title: "垂直插件（SQLite / Registry / VHDX）",
    body: [
      "备份页「高级选项」或作业编辑中可勾选垂直插件：sqlite_checkpoint 对 WAL 数据库做 checkpoint 并跳过 -wal/-shm。",
      "registry_hive（Windows）导出 SYSTEM/SOFTWARE/HKCU hive 至源目录 .ebbackup/registry/ 并纳入备份。",
      "vhdx_scan（Windows）只读挂载源树内 .vhdx 并扫描挂载点内容；可能需要管理员或 Hyper-V 组件。",
      "插件失败会写入备份报告 issues，默认不中断整次备份。",
    ],
  },
  {
    id: "dryrun",
    title: "Dry run 维护操作",
    body: [
      "快照 Prune、Compact、GC 默认 Dry run：只报告将删除/回收的量，不改动数据。",
      "确认结果后再关闭 Dry run 并二次确认执行。",
    ],
  },
];

export const ACTIVITY_GUIDES: Record<ActivityId, HelpSection> = {
  repo: {
    id: "repo",
    title: "仓库",
    body: [
      "新建：父目录 + 名称 → 创建并打开。",
      "打开：选择已有 repo 根目录（含 superblock 的文件夹）。",
      "RPO 卡片：上次成功备份时间、距今天数、WORM 保护数；超阈值时在设置中配置的天数触发本地告警。",
      "侧栏与主页「最近仓库」可快速重开。",
      "「兼容 v0.3 布局」仅在与旧仓库对接时开启。",
    ],
  },
  backup: {
    id: "backup",
    title: "备份",
    body: [
      "源目录：要备份的文件夹，必须存在。",
      "LZ4 + Pipeline：推荐保持开启；高级可试 zstd、耐久性模式。",
      "加密：AES-GCM；过滤器：可选规则文件。",
      "「分析源目录」可生成智能排除建议（.git、node_modules 等），确认后一键写入作业或会话 filter。",
      "首次备份自动全量；再次备份可观察输出区「块复用率」。",
      "备份完成后可在输出区「备份报告」查看未完全备份路径（锁定/权限/不可读等）。",
      "高级选项可配置 Pre/Post Hook shell 命令（自行承担命令风险）。",
      "已保存作业：「入队」写入持久化 job queue；「运行」立即执行。队列面板可「运行下一项」或「Drain 全部」。",
    ],
  },
  snapshots: {
    id: "snapshots",
    title: "快照",
    body: [
      "列表展示 txn_id、时间与文件数；点击行可跳转恢复。",
      "「链可达」徽章：manifest 引用的 chunk 是否均在仓库中（verify-chain JSON）。缺块时恢复按钮禁用。",
      "「内容」按钮可查看该快照备份了哪些文件。",
      "GFS Tiers 示例：1d:7,1w:4,1m:6 表示每日 7 份、每周 4 份、每月 6 份。",
      "先 Dry run 查看将删除的快照数，再正式 Prune。",
    ],
  },
  sync: {
    id: "sync",
    title: "同步",
    body: [
      "无云推荐：「摆渡」导出 delta.ebb 物理搬运后对端导入；或「本地镜像」Push 到 NAS/共享目录。",
      "保存同步配置：摆渡模式（--mode ferry）或指定本地镜像目录（--local-mirror）。",
      "摆渡向导：选择输出目录 → 导出 Delta → 对端导入（base + delta → 新仓库）。",
      "本地镜像：Push 将 chunk/meta 写入镜像目录；对端可用 eb-sync pull 还原。",
      "维护前若存在未导出/未同步 txn，compact/GC 会被门控拦截。",
      "S3 在线同步为可选项，需另行配置 sync.json 与凭证。",
    ],
  },
  browse: {
    id: "browse",
    title: "内容",
    body: [
      "读取 manifest 清单，展示仓库中已备份的文件与目录（相对路径）。",
      "可多选路径后「恢复选中」，或单行「恢复」跳转恢复页并预填 filter。",
      "Txn 留空 = 当前最新 manifest；路径前缀服务端分页，支持「加载更多」。",
      "可多选后「恢复选中」；「路径历史」抽屉支持分页查看各 txn 版本。",
    ],
  },
  diff: {
    id: "diff",
    title: "对比",
    body: [
      "选择两个快照 Txn，生成 entry 级 Diff：新增、删除、修改。",
      "Diff 三表展示新增/删除/修改；含 Merkle 证明步数与 chunk 复用率。",
      "快照页「对比」按钮可快速跳转并预填相邻 txn。",
    ],
  },
  restore: {
    id: "restore",
    title: "恢复",
    body: [
      "恢复模式：「新目录」还原到独立输出路径；「就地恢复」写回 live 源树（与 CLI --in-place 一致）。",
      "目标目录：新目录模式为输出位置；就地模式为 live 源树根目录。",
      "来自内容页的多选路径会作为选择性恢复 filter；也可用手动 .filter 文件（新目录模式）。",
      "就地预览：展示逐路径 diff（新增/修改/冲突/双方变更/孤儿等）；支持三路合并（Base 快照 + target + live）。",
      "冲突筛选与 bulk：可按全部/仅冲突/仅变更/仅新增筛选；支持「应用非冲突」「冲突用快照」「冲突保留 live」与 Dry-run。",
      "冲突策略：跳过、报错或覆盖（overwrite 对 conflict/both_changed 生效）；孤儿策略默认仅报告或删除。",
      "布局重整（新目录）：去前缀 / 扁平化 / 前缀映射；冲突可选报错、跳过或后缀。",
      "恢复完成后可在输出区「验收报告」或「结果」查看 JSON。",
      "Windows：ACL 策略可选继承 / 保留 / 跳过 / 尽力（best_effort，失败写入验收 issues）。",
      "Windows：联接点恢复可选跳过（仅还原目标）或重建 junction（reparse_policy）。",
    ],
  },
  verify: {
    id: "verify",
    title: "验证",
    body: [
      "默认：manifest + chunk 深度校验；加密仓库须填备份密码。",
      "强制锚点：CARL 签名验证，须在验证页填写审计密钥。",
      "Recover：修复中断事务，便于继续增量备份。",
      "维护操作审计：Prune/GC/Compact/就地 Apply（非 dry-run）写入 RAR chain（kind=ops）；本页可刷新列表。",
      "指定 Txn 验证时「强制锚点」不生效（引擎限制）。",
    ],
  },
  maintenance: {
    id: "maintenance",
    title: "维护",
    body: [
      "统计：物理占用、有效数据、孤儿块、放大率。",
      "孤儿解释：按无引用/墓碑/中断提示分类，展示样本 chunk 与 Prune→GC 流程说明。",
      "重整向导：Dry run 或执行 Prune → GC → Compact 全流程。",
      "也可单独 Compact / GC；均建议先 Dry run。",
      "破坏性操作（非 dry-run）会 append-only 写入 audit/rar.chain（kind=ops）；验证页可刷新查看。",
    ],
  },
};
