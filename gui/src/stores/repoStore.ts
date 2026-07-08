import { defineStore } from "pinia";
import {
  closeRepo,
  getProfileState,
  initRepo,
  listSnapshots,
  openRepo,
  repoInfo,
  setProfileState,
  type ProfileStateDto,
  type RepoInfoDto,
  type SnapshotDto,
} from "@/api/ebbackup";
import { useUiStore } from "@/stores/uiStore";
import { useJobStore } from "@/stores/jobStore";
import { useProfileStore } from "@/stores/profileStore";
import { isTauriRuntime } from "@/utils/tauriRuntime";

const LS_RECENT = "ebbackup_workbench_recent";
const LS_LAST_SOURCE = "ebbackup_workbench_last_source";

export const useRepoStore = defineStore("repo", {
  state: () => ({
    isOpen: false,
    path: "",
    info: null as RepoInfoDto | null,
    snapshots: [] as SnapshotDto[],
    recent: [] as string[],
    lastSourcePath: "",
    busy: false,
  }),
  actions: {
    applyProfileState(state: ProfileStateDto) {
      this.recent = state.recentRepos ?? [];
      this.lastSourcePath = state.lastSourcePath ?? "";
    },
    async loadLocal(profileId?: string) {
      if (isTauriRuntime()) {
        try {
          const state = await getProfileState(profileId);
          this.applyProfileState(state);
          return;
        } catch {
          /* fall through */
        }
      }
      try {
        const r = localStorage.getItem(LS_RECENT);
        if (r) this.recent = JSON.parse(r) as string[];
        this.lastSourcePath = localStorage.getItem(LS_LAST_SOURCE) ?? "";
      } catch {
        this.recent = [];
      }
    },
    async persistLocalState() {
      if (isTauriRuntime()) {
        const profile = useProfileStore();
        await setProfileState(profile.activeProfileId, {
          recentRepos: this.recent,
          lastSourcePath: this.lastSourcePath,
        });
        return;
      }
      localStorage.setItem(LS_RECENT, JSON.stringify(this.recent));
      localStorage.setItem(LS_LAST_SOURCE, this.lastSourcePath);
    },
    remember(path: string) {
      const norm = path.trim();
      if (!norm) return;
      this.recent = [norm, ...this.recent.filter((p) => p !== norm)].slice(0, 12);
      void this.persistLocalState();
    },
    setLastSource(path: string) {
      this.lastSourcePath = path;
      void this.persistLocalState();
    },
    async createRepo(parentDir: string, name: string, flags = 0) {
      const ui = useUiStore();
      const path = `${parentDir.replace(/[/\\]+$/, "")}\\${name}`.replace(/\//g, "\\");
      ui.pushLog(`初始化仓库: ${path}`, "cmd");
      this.busy = true;
      try {
        await initRepo(path, flags);
        this.remember(path);
        await this.open(path);
        ui.pushLog("仓库创建并打开成功", "success");
      } finally {
        this.busy = false;
      }
    },
    async open(path: string) {
      const ui = useUiStore();
      ui.pushLog(`打开仓库: ${path}`, "cmd");
      this.busy = true;
      try {
        await openRepo(path);
        this.isOpen = true;
        this.path = path;
        this.remember(path);
        try {
          await this.refreshInfo();
          await this.refreshSnapshots();
          await useJobStore().refreshJobs();
        } catch (e) {
          await this.close();
          const msg = e instanceof Error ? e.message : String(e);
          throw new Error(msg.includes("manifest") ? "仓库尚未完成首次备份（缺少 manifest）" : msg);
        }
        ui.pushLog("仓库已打开", "success");
      } finally {
        this.busy = false;
      }
    },
    async close() {
      const ui = useUiStore();
      await closeRepo();
      this.isOpen = false;
      this.path = "";
      this.info = null;
      this.snapshots = [];
      useJobStore().clear();
      ui.pushLog("仓库已关闭", "meta");
    },
    async refreshInfo() {
      if (!this.isOpen) return;
      try {
        this.info = await repoInfo();
      } catch (e) {
        this.info = null;
        throw e;
      }
    },
    async refreshSnapshots() {
      if (!this.isOpen) return;
      this.snapshots = await listSnapshots();
    },
  },
});
