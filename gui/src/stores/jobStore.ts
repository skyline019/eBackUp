import { defineStore } from "pinia";
import {
  deleteJob,
  listJobs,
  runJob,
  upsertJob,
  type BackupJobDto,
  type BackupStatsDto,
} from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";

export const useJobStore = defineStore("job", {
  state: () => ({
    jobs: [] as BackupJobDto[],
    busy: false,
  }),
  actions: {
    clear() {
      this.jobs = [];
    },
    async refreshJobs() {
      const repo = useRepoStore();
      if (!repo.isOpen) {
        this.jobs = [];
        return;
      }
      this.jobs = await listJobs();
    },
    async upsertJob(job: BackupJobDto) {
      this.busy = true;
      try {
        await upsertJob(job);
        await this.refreshJobs();
      } finally {
        this.busy = false;
      }
    },
    async deleteJob(jobId: string) {
      this.busy = true;
      try {
        await deleteJob(jobId);
        await this.refreshJobs();
      } finally {
        this.busy = false;
      }
    },
    async runJob(
      jobId: string,
      incremental?: boolean,
      flags?: number
    ): Promise<BackupStatsDto> {
      this.busy = true;
      try {
        return await runJob(jobId, incremental, flags);
      } finally {
        this.busy = false;
      }
    },
  },
});
