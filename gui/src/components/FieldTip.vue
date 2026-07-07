<script setup lang="ts">
defineProps<{ content: string }>();
</script>

<template>
  <el-tooltip :content="content" placement="top" :show-after="350" max-width="320">
    <span class="field-tip" tabindex="0" aria-label="帮助">?</span>
  </el-tooltip>
</template>

<style scoped>
.field-tip {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 16px;
  height: 16px;
  margin-left: 6px;
  border-radius: 50%;
  font-size: 10px;
  font-weight: 700;
  color: var(--text-soft);
  border: 1px solid var(--shell-line);
  cursor: help;
  vertical-align: middle;
  user-select: none;
}
.field-tip:hover {
  color: var(--accent);
  border-color: var(--accent);
}
</style>
