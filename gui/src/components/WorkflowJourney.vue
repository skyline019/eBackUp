<script setup lang="ts">
import { computed } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { ACTIVITIES, type ActivityId } from "@/utils/activities";

const ui = useUiStore();

const steps = ACTIVITIES.map((a) => ({
  id: a.id as ActivityId,
  label: a.label,
  hint: a.hint,
}));

const currentIdx = computed(() => steps.findIndex((s) => s.id === ui.activity));
</script>

<template>
  <nav class="workflow-journey" aria-label="备份工作流">
    <template v-for="(step, i) in steps" :key="step.id">
      <button
        type="button"
        class="wj-step"
        :class="{
          'is-current': step.id === ui.activity,
          'is-past': i < currentIdx,
        }"
        :title="step.hint"
        @click="ui.setActivity(step.id)"
      >
        <span class="wj-dot">{{ i + 1 }}</span>
        <span class="wj-label">{{ step.label }}</span>
      </button>
      <span v-if="i < steps.length - 1" class="wj-arrow" aria-hidden="true">→</span>
    </template>
  </nav>
</template>

<style scoped>
.workflow-journey {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 4px 2px;
  padding: 8px 20px 4px;
  border-bottom: 1px solid var(--shell-line);
  background: color-mix(in srgb, var(--panel-bg-strong) 55%, transparent);
}
.wj-step {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  border: none;
  background: transparent;
  cursor: pointer;
  padding: 4px 8px;
  border-radius: 8px;
  color: var(--text-soft);
  font-size: 11px;
  transition: background 0.15s, color 0.15s;
}
.wj-step:hover {
  background: var(--hover-bg);
  color: var(--text-regular);
}
.wj-step.is-past .wj-dot {
  background: color-mix(in srgb, #22c55e 25%, transparent);
  border-color: #22c55e;
  color: #86efac;
}
.wj-step.is-current {
  color: var(--text-main);
  background: color-mix(in srgb, var(--accent) 12%, transparent);
}
.wj-step.is-current .wj-dot {
  border-color: var(--accent);
  color: var(--accent);
  box-shadow: 0 0 0 2px color-mix(in srgb, var(--accent) 20%, transparent);
}
.wj-dot {
  width: 20px;
  height: 20px;
  border-radius: 50%;
  border: 1.5px solid var(--shell-line);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 10px;
  font-weight: 700;
}
.wj-label {
  white-space: nowrap;
}
.wj-arrow {
  color: var(--text-soft);
  opacity: 0.45;
  font-size: 10px;
  user-select: none;
}
</style>
