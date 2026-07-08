import { defineStore } from "pinia";
import {
  createProfile,
  getActiveProfile,
  getProfileState,
  listProfiles,
  setProfileState,
  switchProfile,
  type ProfileDto,
  type ProfileStateDto,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { isTauriRuntime } from "@/utils/tauriRuntime";

export const useProfileStore = defineStore("profile", {
  state: () => ({
    activeProfileId: "default",
    profiles: [] as ProfileDto[],
    loaded: false,
  }),
  getters: {
    activeProfile(state): ProfileDto | undefined {
      return state.profiles.find((p) => p.id === state.activeProfileId);
    },
  },
  actions: {
    async load() {
      if (!isTauriRuntime()) {
        this.activeProfileId = "default";
        this.profiles = [{ id: "default", name: "Default", createdAtUnix: 0 }];
        this.loaded = true;
        return;
      }
      const res = await listProfiles();
      this.activeProfileId = res.activeProfileId;
      this.profiles = res.profiles;
      this.loaded = true;
    },
    async refresh() {
      if (!isTauriRuntime()) return;
      const res = await listProfiles();
      this.activeProfileId = res.activeProfileId;
      this.profiles = res.profiles;
    },
    async create(name: string) {
      const res = await createProfile(name);
      await this.refresh();
      if (res.profile?.id) {
        await this.switchTo(res.profile.id);
      }
    },
    async switchTo(profileId: string) {
      if (!isTauriRuntime() || profileId === this.activeProfileId) return;
      const ui = useUiStore();
      const repo = useRepoStore();

      if (repo.isOpen) {
        await repo.close();
      }

      await this.persistRepoState();

      const res = await switchProfile(profileId);
      this.activeProfileId = res.profileId;
      await this.refresh();

      if (res.settings) {
        ui.applyProfileSettings(res.settings);
      } else {
        await ui.load(profileId);
      }

      const state = (res.state as ProfileStateDto | undefined) ?? (await getProfileState(profileId));
      repo.applyProfileState(state);

      const lastRepo = this.activeProfile?.lastRepoPath;
      if (lastRepo) {
        try {
          await repo.open(lastRepo);
        } catch (e) {
          ui.pushLog(String(e), "error");
        }
      }
    },
    async persistRepoState() {
      if (!isTauriRuntime()) return;
      const repo = useRepoStore();
      await setProfileState(this.activeProfileId, {
        recentRepos: repo.recent,
        lastSourcePath: repo.lastSourcePath,
      });
    },
    async ensureActive() {
      if (!this.loaded) await this.load();
      if (!isTauriRuntime()) return;
      const id = await getActiveProfile();
      this.activeProfileId = id;
    },
  },
});
