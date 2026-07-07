<script setup lang="ts">
defineProps<{
  title: string;
  hint?: string;
  actionLabel?: string;
}>();

const emit = defineEmits<{ (e: "action"): void }>();
</script>

<template>
  <div class="empty-state">
    <p class="empty-title">{{ title }}</p>
    <p v-if="hint" class="empty-hint">{{ hint }}</p>
    <el-button v-if="actionLabel" type="primary" link @click="emit('action')">
      {{ actionLabel }}
    </el-button>
  </div>
</template>

<style scoped>
.empty-state {
  padding: 20px 0 8px;
  text-align: center;
}
.empty-title {
  margin: 0;
  font-size: 14px;
  color: var(--text-main);
}
.empty-hint {
  margin: 8px 0 0;
  font-size: 12px;
  color: var(--text-soft);
  line-height: 1.5;
}
</style>
