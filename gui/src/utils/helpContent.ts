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
      "3. 快照页：刷新查看 txn 列表。",
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
      "DLL 与 eb 命令行共用同一 C++ 内核（ABI v12）。",
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
    id: "transparency",
    title: "壁纸模式透明度",
    body: [
      "设置 → 壁纸：上传图片/视频后，工作区右上角「卡片」与输出区「输出」滑块调节透明度。",
      "若看起来不透明，确认已启用壁纸且滑块低于 100%。",
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
      "菜单「运行备份」在已填源目录时可直接执行。",
    ],
  },
  snapshots: {
    id: "snapshots",
    title: "快照",
    body: [
      "列表展示 txn_id、时间与文件数；点击行可跳转恢复。",
      "GFS Tiers 示例：1d:7,1w:4,1m:6 表示每日 7 份、每周 4 份、每月 6 份。",
      "先 Dry run 查看将删除的快照数，再正式 Prune。",
    ],
  },
  restore: {
    id: "restore",
    title: "恢复",
    body: [
      "目标目录：还原输出位置（可不存在，将创建）。",
      "Txn 留空 = 最新快照；侧栏/快照表可快速预选。",
      "加密仓库须填密码；可选过滤器与「跳过内容校验」（高级）。",
    ],
  },
  verify: {
    id: "verify",
    title: "验证",
    body: [
      "默认：manifest + chunk 深度校验，无需密钥。",
      "强制锚点：CARL 签名验证，须在验证页填写审计密钥。",
      "Recover：修复中断事务，便于继续增量备份。",
      "指定 Txn 验证时「强制锚点」不生效（引擎限制）。",
    ],
  },
  maintenance: {
    id: "maintenance",
    title: "维护",
    body: [
      "统计：物理占用、有效数据、孤儿块、放大率。",
      "Compact：合并 EbPack 降低放大率；GC：回收无引用孤儿块。",
      "两项均建议先 Dry run，结果见底部「任务」面板。",
    ],
  },
};
