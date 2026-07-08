export type MenuAction =
  | "open-repo"
  | "close-repo"
  | "init-repo"
  | "run-backup"
  | "verify"
  | "recover"
  | "open-help"
  | "toggle-settings"
  | "toggle-sidebar"
  | "toggle-log"
  | "goto-repo"
  | "goto-backup"
  | "goto-snapshots"
  | "goto-browse"
  | "goto-restore"
  | "goto-verify"
  | "goto-maintenance";

export interface MenuNode {
  label?: string;
  action?: MenuAction;
  divider?: boolean;
  section?: boolean;
  children?: MenuNode[];
}

export interface TopMenu {
  key: string;
  label: string;
  items: MenuNode[];
}

export const topMenus: TopMenu[] = [
  {
    key: "file",
    label: "文件",
    items: [
      { label: "打开仓库…", action: "open-repo" },
      { label: "关闭仓库", action: "close-repo" },
      { divider: true },
      { label: "新建仓库向导", action: "init-repo" },
    ],
  },
  {
    key: "backup",
    label: "备份",
    items: [
      { label: "运行备份", action: "run-backup" },
      { label: "快照列表", action: "goto-snapshots" },
      { label: "查看备份内容", action: "goto-browse" },
      { label: "还原…", action: "goto-restore" },
    ],
  },
  {
    key: "tools",
    label: "工具",
    items: [
      { label: "验证仓库", action: "verify" },
      { label: "修复中断事务", action: "recover" },
      { divider: true },
      { label: "维护面板", action: "goto-maintenance" },
    ],
  },
  {
    key: "view",
    label: "视图",
    items: [
      { label: "切换侧栏", action: "toggle-sidebar" },
      { label: "切换输出面板", action: "toggle-log" },
      { divider: true },
      { label: "设置…", action: "toggle-settings" },
    ],
  },
  {
    key: "help",
    label: "帮助",
    items: [
      { label: "帮助中心…", action: "open-help" },
      { label: "快速入门", action: "open-help" },
    ],
  },
];
