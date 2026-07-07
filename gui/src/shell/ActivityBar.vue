<script setup lang="ts">
import { ACTIVITIES, type ActivityId } from "@/utils/activities";
import { useUiStore } from "@/stores/uiStore";
import {
  Coin,
  Upload,
  Clock,
  Download,
  DocumentChecked,
  Tools,
} from "@element-plus/icons-vue";

const ui = useUiStore();

const icons: Record<ActivityId, object> = {
  repo: Coin,
  backup: Upload,
  snapshots: Clock,
  restore: Download,
  verify: DocumentChecked,
  maintenance: Tools,
};
</script>

<template>
  <nav class="activity-bar" aria-label="活动栏">
    <button
      v-for="a in ACTIVITIES"
      :key="a.id"
      type="button"
      class="activity-btn"
      :class="{ active: ui.activity === a.id }"
      :title="a.hint"
      @click="ui.setActivity(a.id)"
    >
      <component :is="icons[a.id]" class="activity-icon" />
      <span class="activity-label">{{ a.label }}</span>
    </button>
  </nav>
</template>

<style scoped>
.activity-bar {
  display: flex;
  flex-direction: column;
  gap: 4px;
  width: 56px;
  padding: 8px 4px;
  background: color-mix(in srgb, var(--panel-bg-strong) 90%, transparent);
  border-right: 1px solid var(--shell-line, rgba(148, 163, 184, 0.12));
}
.activity-btn {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  padding: 8px 4px;
  border: none;
  border-radius: 8px;
  background: transparent;
  color: var(--text-soft);
  cursor: pointer;
  font-size: 10px;
}
.activity-btn:hover {
  background: var(--hover-bg);
  color: var(--text-main);
}
.activity-btn.active {
  background: var(--active-bg);
  color: var(--accent);
  box-shadow: inset 2px 0 0 var(--accent);
}
.activity-icon {
  width: 18px;
  height: 18px;
}
.activity-label {
  line-height: 1.1;
  text-align: center;
}
</style>
