<script setup lang="ts">
import { computed } from "vue";
import { useTaskStore } from "@/stores/taskStore";

const task = useTaskStore();

const phases = computed(() => task.phases);
const active = computed(() => task.active);

function stepClass(i: number) {
  if (!active.value) return "";
  if (active.value.status === "error" && i === active.value.phaseIndex) return "is-error";
  if (i < active.value.phaseIndex) return "is-done";
  if (i === active.value.phaseIndex) return "is-active";
  return "is-pending";
}
</script>

<template>
  <div v-if="active && phases.length" class="process-pipeline">
    <div
      v-for="(phase, i) in phases"
      :key="phase.id"
      class="pipe-step"
      :class="stepClass(i)"
    >
      <div class="pipe-node">
        <span class="pipe-index">{{ i + 1 }}</span>
      </div>
      <div class="pipe-label">{{ phase.label }}</div>
      <div v-if="i < phases.length - 1" class="pipe-connector" />
    </div>
  </div>
</template>

<style scoped>
.process-pipeline {
  display: flex;
  align-items: flex-start;
  gap: 0;
  padding: 4px 0 2px;
  overflow-x: auto;
}
.pipe-step {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 88px;
  flex: 1;
}
.pipe-node {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: 2px solid var(--shell-line);
  display: flex;
  align-items: center;
  justify-content: center;
  background: color-mix(in srgb, var(--panel-bg-strong) 70%, transparent);
  transition: border-color 0.25s, box-shadow 0.25s, background 0.25s;
}
.pipe-index {
  font-size: 11px;
  font-weight: 700;
  color: var(--text-soft);
}
.pipe-label {
  margin-top: 6px;
  font-size: 10px;
  text-align: center;
  color: var(--text-soft);
  line-height: 1.3;
  max-width: 92px;
}
.pipe-connector {
  position: absolute;
  top: 14px;
  left: calc(50% + 16px);
  width: calc(100% - 32px);
  height: 2px;
  background: var(--shell-line);
  z-index: 0;
}
.pipe-step.is-done .pipe-node {
  border-color: #22c55e;
  background: color-mix(in srgb, #22c55e 18%, transparent);
}
.pipe-step.is-done .pipe-index {
  color: #86efac;
}
.pipe-step.is-done .pipe-label {
  color: var(--text-regular);
}
.pipe-step.is-active .pipe-node {
  border-color: var(--accent);
  box-shadow: 0 0 0 3px color-mix(in srgb, var(--accent) 25%, transparent);
  animation: pulse-node 1.4s ease-in-out infinite;
}
.pipe-step.is-active .pipe-index {
  color: var(--accent);
}
.pipe-step.is-active .pipe-label {
  color: var(--text-main);
  font-weight: 600;
}
.pipe-step.is-error .pipe-node {
  border-color: #ef4444;
  background: color-mix(in srgb, #ef4444 15%, transparent);
}
@keyframes pulse-node {
  0%,
  100% {
    transform: scale(1);
  }
  50% {
    transform: scale(1.06);
  }
}
</style>
