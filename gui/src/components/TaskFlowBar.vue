<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { useTaskStore } from "@/stores/taskStore";
import ProcessPipeline from "@/components/ProcessPipeline.vue";

const task = useTaskStore();
const tick = ref(0);
let timer: ReturnType<typeof setInterval> | null = null;

onMounted(() => {
  timer = setInterval(() => {
    if (task.isRunning) tick.value++;
  }, 1000);
});
onUnmounted(() => {
  if (timer) clearInterval(timer);
});

const active = computed(() => task.active);
const barStatus = computed(() => {
  if (!active.value) return "";
  if (active.value.status === "error") return "exception";
  if (active.value.status === "success") return "success";
  return "";
});

const elapsed = computed(() => {
  void tick.value;
  if (!active.value) return "";
  const sec = Math.floor((Date.now() - active.value.startedAt) / 1000);
  return `${sec}s`;
});
</script>

<template>
  <transition name="task-bar">
    <section v-if="active" class="task-flow-bar" :class="`status-${active.status}`">
      <div class="task-head">
        <span class="task-title">{{ active.label }}</span>
        <span class="task-phase">{{ active.message }}</span>
        <span class="task-elapsed">{{ elapsed }}</span>
        <span class="task-pct">{{ active.percent.toFixed(1) }}%</span>
      </div>
      <el-progress
        :percentage="active.percent"
        :stroke-width="8"
        :status="barStatus || undefined"
        :indeterminate="active.status === 'running' && active.percent < 2 && active.kind !== 'backup'"
        striped
        striped-flow
      />
      <ProcessPipeline />
    </section>
  </transition>
</template>

<style scoped>
.task-flow-bar {
  margin: 0 20px 10px;
  padding: 12px 14px 10px;
  border-radius: 10px;
  border: 1px solid color-mix(in srgb, var(--accent) 35%, var(--shell-line));
  background: color-mix(in srgb, var(--accent) 8%, var(--panel-bg-strong, rgba(9, 17, 36, 0.92)));
  box-shadow: 0 4px 24px color-mix(in srgb, var(--accent) 12%, transparent);
}
.task-head {
  display: flex;
  align-items: baseline;
  gap: 10px;
  margin-bottom: 8px;
  flex-wrap: wrap;
}
.task-title {
  font-size: 13px;
  font-weight: 700;
  color: var(--text-main);
}
.task-phase {
  font-size: 12px;
  color: var(--text-regular);
  flex: 1;
}
.task-elapsed,
.task-pct {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-soft);
}
.status-error {
  border-color: color-mix(in srgb, #ef4444 45%, var(--shell-line));
  background: color-mix(in srgb, #ef4444 8%, var(--panel-bg-strong));
}
.task-bar-enter-active,
.task-bar-leave-active {
  transition: opacity 0.28s ease, transform 0.28s ease;
}
.task-bar-enter-from,
.task-bar-leave-to {
  opacity: 0;
  transform: translateY(-8px);
}
</style>
