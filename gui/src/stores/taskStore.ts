import { defineStore } from "pinia";
import {
  phasesForKind,
  phaseIndexForPermille,
  percentFromPermille,
  TASK_LABELS,
  type TaskKind,
} from "@/utils/taskPhases";

export type TaskStatus = "idle" | "running" | "success" | "error";

export interface ActiveTask {
  kind: TaskKind;
  label: string;
  permille: number;
  percent: number;
  phaseIndex: number;
  status: TaskStatus;
  startedAt: number;
  message: string;
}

let simTimer: ReturnType<typeof setInterval> | null = null;

export const useTaskStore = defineStore("task", {
  state: () => ({
    active: null as ActiveTask | null,
    lastFinished: null as { kind: TaskKind; ok: boolean; at: number } | null,
    hasNativeProgress: false,
  }),
  getters: {
    isRunning(state): boolean {
      return state.active?.status === "running";
    },
    phases(state) {
      if (!state.active) return [];
      return phasesForKind(state.active.kind);
    },
  },
  actions: {
    start(kind: TaskKind) {
      this.stopSim();
      this.hasNativeProgress = kind === "backup";
      this.active = {
        kind,
        label: TASK_LABELS[kind],
        permille: 0,
        percent: 0,
        phaseIndex: 0,
        status: "running",
        startedAt: Date.now(),
        message: "准备中…",
      };
      if (!this.hasNativeProgress) {
        this.startSim();
      }
      if (!this.hasNativeProgress) {
        this.expandOutput();
      }
    },
    updatePermille(permille: number) {
      if (!this.active) return;
      this.hasNativeProgress = true;
      this.stopSim();
      const phases = phasesForKind(this.active.kind);
      const idx = phaseIndexForPermille(phases, permille);
      this.active.permille = permille;
      this.active.percent = percentFromPermille(permille);
      this.active.phaseIndex = idx;
      this.active.message = phases[idx]?.label ?? "";
    },
    finish(ok: boolean) {
      if (!this.active) return;
      this.stopSim();
      this.active.permille = ok ? 1000 : this.active.permille;
      this.active.percent = ok ? 100 : this.active.percent;
      this.active.status = ok ? "success" : "error";
      this.active.message = ok ? "完成" : "失败";
      this.lastFinished = { kind: this.active.kind, ok, at: Date.now() };
      setTimeout(() => {
        if (this.active?.status !== "running") {
          this.active = null;
        }
      }, ok ? 2200 : 4000);
    },
    expandOutput() {
      /* uiStore wired from composable */
    },
    startSim() {
      this.stopSim();
      simTimer = setInterval(() => {
        if (!this.active || this.active.status !== "running") return;
        const phases = phasesForKind(this.active.kind);
        const next = Math.min(900, this.active.permille + 18 + Math.floor(Math.random() * 25));
        const idx = Math.min(
          phases.length - 1,
          Math.floor((next / 1000) * phases.length)
        );
        this.active.permille = next;
        this.active.percent = percentFromPermille(next);
        this.active.phaseIndex = idx;
        this.active.message = phases[idx]?.label ?? "";
      }, 420);
    },
    stopSim() {
      if (simTimer) {
        clearInterval(simTimer);
        simTimer = null;
      }
    },
    reset() {
      this.stopSim();
      this.active = null;
    },
  },
});
