/** Theme / wallpaper settings (ebbackup Workbench — eB-Tree visual lineage). */

export type BgMode = "gradient" | "image" | "video";

export interface UiSettings {
  accent: string;
  textMain: string;
  textRegular: string;
  textSoft: string;
  pageBg: string;
  tableBg: string;
  bgMode: BgMode;
  bgImageUrl: string;
  bgImageOpacity: number;
  bgVideoUrl: string;
  bgVideoOpacity: number;
  panelOpacity: number;
  workspaceCardOpacity: number;
  tableViewOpacity: number;
  logPanelOpacity: number;
  sidebarWidth: number;
  logCollapsed: boolean;
  denseMode: boolean;
  fontScale: number;
  animations: boolean;
  cornerScale: number;
  shadowScale: number;
  logFontScale: number;
  logLineHeight: number;
  borderContrast: number;
  panelBrightness: number;
  logHighlightIntensity: number;
}

/** Full-range opacity for workspace cards & output panel (5%–100%). */
export const OPACITY_FULL = { min: 0.05, max: 1.0 } as const;

/** Slider / sanitize caps for panel opacities. */
export const OPACITY_LIMITS = {
  panel: { min: 0.35, max: 0.96 },
  workspaceCard: OPACITY_FULL,
  tableView: { min: 0.35, max: 0.88 },
  logPanel: OPACITY_FULL,
} as const;

/** 壁纸模式下可读性下限 + 分层透明度（主视图 vs 边框装饰区） */
export const WALLPAPER_READABILITY_FLOORS = {
  panel: 0.62,
  tableView: 0.56,
  logPanel: 0.68,
  /** 主工作区：略透，但仍可读 */
  workspace: 0.46,
  /** 右侧工具栏/筛选/分页等边框容器：更透 */
  chrome: 0.26,
} as const;

export const defaultUiSettings: UiSettings = {
  accent: "#3b82f6",
  textMain: "#dbe7ff",
  textRegular: "#c7d2fe",
  textSoft: "#93c5fd",
  pageBg: "#080d18",
  tableBg: "#08121f",
  bgMode: "gradient",
  bgImageUrl: "",
  bgImageOpacity: 0.58,
  bgVideoUrl: "",
  bgVideoOpacity: 0.58,
  panelOpacity: 0.86,
  workspaceCardOpacity: 0.88,
  tableViewOpacity: 0.74,
  logPanelOpacity: 0.9,
  sidebarWidth: 260,
  logCollapsed: false,
  denseMode: false,
  fontScale: 1,
  animations: true,
  cornerScale: 1,
  shadowScale: 1,
  logFontScale: 1,
  logLineHeight: 1.5,
  borderContrast: 1,
  panelBrightness: 1,
  logHighlightIntensity: 1,
};

export type SettingsPreset = {
  key: string;
  label: string;
  settings: Partial<UiSettings>;
};

export const settingsPresets: SettingsPreset[] = [
  {
    key: "default",
    label: "默认",
    settings: { ...defaultUiSettings, bgMode: "gradient" },
  },
  {
    key: "glass",
    label: "壁纸可读",
    settings: {
      bgMode: "image",
      panelOpacity: WALLPAPER_READABILITY_FLOORS.panel,
      workspaceCardOpacity: WALLPAPER_READABILITY_FLOORS.workspace,
      tableViewOpacity: WALLPAPER_READABILITY_FLOORS.tableView,
      logPanelOpacity: WALLPAPER_READABILITY_FLOORS.logPanel,
      bgImageOpacity: 0.62,
      bgVideoOpacity: 0.62,
    },
  },
  {
    key: "midnight",
    label: "午夜蓝",
    settings: {
      accent: "#2563eb",
      pageBg: "#050a14",
      tableBg: "#071018",
      textMain: "#e2e8f0",
      textRegular: "#cbd5e1",
      textSoft: "#94a3b8",
      bgMode: "gradient",
    },
  },
  {
    key: "mint",
    label: "薄荷绿",
    settings: {
      accent: "#10b981",
      pageBg: "#061210",
      tableBg: "#081816",
      bgMode: "gradient",
    },
  },
];

function clamp(n: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, n));
}

/** Precomputed rgba — slider value maps 1:1 to alpha (5% slider → 5% opaque). */
export function panelSurfaceRgba(
  opacity: number,
  _brightness: number,
  rgb = "9, 17, 36"
): string {
  const a = clamp(opacity, 0, 1);
  return `rgba(${rgb}, ${a.toFixed(4)})`;
}

function hasWallpaperInSettings(s: Pick<UiSettings, "bgMode" | "bgImageUrl" | "bgVideoUrl">): boolean {
  return (
    (s.bgMode === "image" && s.bgImageUrl.trim().length > 0) ||
    (s.bgMode === "video" && s.bgVideoUrl.trim().length > 0)
  );
}

/** 壁纸开启时抬高部分面板不透明度；工作区卡片与输出面板由用户全范围调节，不参与下限 */
export function applyWallpaperOpacityFloors(s: UiSettings): UiSettings {
  if (!hasWallpaperInSettings(s)) return s;
  const f = WALLPAPER_READABILITY_FLOORS;
  return {
    ...s,
    panelOpacity: Math.max(s.panelOpacity, f.panel),
    tableViewOpacity: Math.max(s.tableViewOpacity, f.tableView),
  };
}

export function effectivePanelOpacities(s: UiSettings): {
  panel: number;
  tableView: number;
  logPanel: number;
  workspace: number;
  chrome: number;
} {
  const lifted = applyWallpaperOpacityFloors(s);
  const f = WALLPAPER_READABILITY_FLOORS;
  const wp = hasWallpaperInSettings(s);
  if (!wp) {
    const chrome = clamp(lifted.panelOpacity * 0.55, 0.38, 0.72);
    return {
      panel: lifted.panelOpacity,
      tableView: lifted.tableViewOpacity,
      logPanel: s.logPanelOpacity,
      workspace: lifted.panelOpacity * 0.82,
      chrome,
    };
  }
  const workspace = clamp(lifted.panelOpacity * 0.54, f.workspace, 0.62);
  const chrome = clamp(lifted.panelOpacity * 0.32, f.chrome, workspace * 0.72);
  return {
    panel: lifted.panelOpacity,
    tableView: lifted.tableViewOpacity,
    logPanel: s.logPanelOpacity,
    workspace,
    chrome,
  };
}

function num(v: unknown, fallback: number): number {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
}

export function normalizeHexColor(raw: string, fallback: string): string {
  const t = String(raw ?? "").trim();
  if (/^#[0-9a-f]{6}$/i.test(t)) return t.toLowerCase();
  if (/^#[0-9a-f]{3}$/i.test(t)) {
    const h = t.slice(1);
    return `#${h[0]}${h[0]}${h[1]}${h[1]}${h[2]}${h[2]}`.toLowerCase();
  }
  return fallback;
}

export function sanitizeUiSettings(input: Partial<UiSettings>): UiSettings {
  const d = defaultUiSettings;
  const bgMode: BgMode =
    input.bgMode === "video" ? "video" : input.bgMode === "image" ? "image" : "gradient";
  const base: UiSettings = {
    accent: normalizeHexColor(String(input.accent ?? ""), d.accent),
    textMain: normalizeHexColor(String(input.textMain ?? ""), d.textMain),
    textRegular: normalizeHexColor(String(input.textRegular ?? ""), d.textRegular),
    textSoft: normalizeHexColor(String(input.textSoft ?? ""), d.textSoft),
    pageBg: normalizeHexColor(String(input.pageBg ?? ""), d.pageBg),
    tableBg: normalizeHexColor(String(input.tableBg ?? ""), d.tableBg),
    bgMode,
    bgImageUrl: String(input.bgImageUrl ?? ""),
    bgImageOpacity: clamp(num(input.bgImageOpacity, d.bgImageOpacity), 0.05, 0.8),
    bgVideoUrl: String(input.bgVideoUrl ?? ""),
    bgVideoOpacity: clamp(num(input.bgVideoOpacity, d.bgVideoOpacity), 0.05, 0.8),
    panelOpacity: clamp(
      num(input.panelOpacity, d.panelOpacity),
      OPACITY_LIMITS.panel.min,
      OPACITY_LIMITS.panel.max
    ),
    workspaceCardOpacity: clamp(
      num(input.workspaceCardOpacity, d.workspaceCardOpacity),
      OPACITY_LIMITS.workspaceCard.min,
      OPACITY_LIMITS.workspaceCard.max
    ),
    tableViewOpacity: clamp(
      num(input.tableViewOpacity, d.tableViewOpacity),
      OPACITY_LIMITS.tableView.min,
      OPACITY_LIMITS.tableView.max
    ),
    logPanelOpacity: clamp(
      num(input.logPanelOpacity, d.logPanelOpacity),
      OPACITY_LIMITS.logPanel.min,
      OPACITY_LIMITS.logPanel.max
    ),
    sidebarWidth: clamp(num(input.sidebarWidth, d.sidebarWidth), 200, 460),
    logCollapsed: Boolean(input.logCollapsed),
    denseMode: Boolean(input.denseMode),
    fontScale: clamp(num(input.fontScale, d.fontScale), 0.85, 1.25),
    animations: input.animations !== false,
    cornerScale: clamp(num(input.cornerScale, d.cornerScale), 0.8, 1.35),
    shadowScale: clamp(num(input.shadowScale, d.shadowScale), 0.6, 1.5),
    logFontScale: clamp(num(input.logFontScale, d.logFontScale), 0.88, 1.25),
    logLineHeight: clamp(num(input.logLineHeight, d.logLineHeight), 1.3, 1.9),
    borderContrast: clamp(num(input.borderContrast, d.borderContrast), 0.75, 1.4),
    panelBrightness: clamp(num(input.panelBrightness, d.panelBrightness), 0.5, 1.2),
    logHighlightIntensity: clamp(
      num(input.logHighlightIntensity, d.logHighlightIntensity),
      0.75,
      1.4
    ),
  };
  return applyWallpaperOpacityFloors(base);
}

export function buildLayoutGradient(page: string, accent: string): string {
  const p = page.trim() || defaultUiSettings.pageBg;
  const a = accent.trim() || defaultUiSettings.accent;
  return [
    `radial-gradient(ellipse 118% 88% at 50% -24%, color-mix(in srgb, ${a} 26%, ${p}) 0%, transparent 57%)`,
    `radial-gradient(ellipse 70% 52% at 100% 34%, color-mix(in srgb, ${a} 13%, ${p}) 0%, transparent 51%)`,
    `radial-gradient(ellipse 58% 44% at 0% 76%, color-mix(in srgb, ${a} 9%, ${p}) 0%, transparent 47%)`,
    `linear-gradient(188deg, color-mix(in srgb, ${p} 95%, #000) 0%, ${p} 40%, color-mix(in srgb, ${p} 97%, #030712) 100%)`,
  ].join(", ");
}

export function layoutStyleFromSettings(s: UiSettings): Record<string, string> {
  const lifted = applyWallpaperOpacityFloors(s);
  const eff = effectivePanelOpacities(s);
  const hasVideo = s.bgMode === "video" && s.bgVideoUrl.trim();
  const hasImage = s.bgMode === "image" && s.bgImageUrl.trim();
  const bg =
    hasVideo || hasImage ? "none" : buildLayoutGradient(s.pageBg, s.accent);
  return {
    "--accent": s.accent,
    "--accent-soft": `color-mix(in srgb, ${s.accent} 45%, #9bf3ff)`,
    "--text-main": s.textMain,
    "--text-regular": s.textRegular,
    "--text-soft": s.textSoft,
    "--page-bg": s.pageBg,
    "--table-surface-bg": s.tableBg,
    "--panel-opacity": String(lifted.panelOpacity),
    "--workspace-card-opacity": String(s.workspaceCardOpacity),
    "--workspace-card-surface": panelSurfaceRgba(s.workspaceCardOpacity, s.panelBrightness),
    "--table-view-opacity": String(lifted.tableViewOpacity),
    "--log-panel-opacity": String(s.logPanelOpacity),
    "--log-panel-surface": panelSurfaceRgba(s.logPanelOpacity, s.panelBrightness, "7, 14, 31"),
    "--panel-opacity-effective": String(eff.panel),
    "--table-view-opacity-effective": String(eff.tableView),
    "--log-panel-opacity-effective": String(eff.logPanel),
    "--workspace-panel-opacity": String(eff.workspace),
    "--chrome-panel-opacity": String(eff.chrome),
    "--font-scale": String(s.fontScale),
    "--sidebar-width": `${Math.round(s.sidebarWidth)}px`,
    "--corner-scale": String(s.cornerScale),
    "--shadow-scale": String(s.shadowScale),
    "--log-font-scale": String(s.logFontScale),
    "--log-line-height": String(s.logLineHeight),
    "--border-contrast": String(s.borderContrast),
    "--panel-brightness": String(s.panelBrightness),
    "--log-highlight-intensity": String(s.logHighlightIntensity),
    "--wallpaper-opacity": String(s.bgMode === "video" ? s.bgVideoOpacity : s.bgImageOpacity),
    backgroundImage: bg,
    backgroundSize: "cover",
    backgroundPosition: "center center",
    backgroundRepeat: "no-repeat",
  };
}

export function hasActiveWallpaper(s: UiSettings): boolean {
  return (
    (s.bgMode === "image" && s.bgImageUrl.trim().length > 0) ||
    (s.bgMode === "video" && s.bgVideoUrl.trim().length > 0)
  );
}
