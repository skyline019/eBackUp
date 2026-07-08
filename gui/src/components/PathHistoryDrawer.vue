<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { queryPathHistory, type PathHistoryEntryDto } from "@/api/ebbackup";

const PAGE_SIZE = 50;

const props = defineProps<{
  modelValue: boolean;
  path: string;
}>();

const emit = defineEmits<{
  "update:modelValue": [value: boolean];
}>();

const loading = ref(false);
const loadingMore = ref(false);
const history = ref<PathHistoryEntryDto[]>([]);
const total = ref(0);
const offset = ref(0);

const hasMore = computed(() => history.value.length < total.value);

watch(
  () => [props.modelValue, props.path] as const,
  () => {
    if (props.modelValue && props.path.trim()) void load(true);
  }
);

async function load(reset: boolean) {
  if (reset) {
    loading.value = true;
    offset.value = 0;
    history.value = [];
    total.value = 0;
  }
  try {
    const res = await queryPathHistory(props.path.trim(), offset.value, PAGE_SIZE);
    const batch = res.history ?? [];
    total.value = res.total ?? res.count ?? batch.length;
    if (reset) {
      history.value = batch;
    } else {
      history.value = history.value.concat(batch);
    }
    offset.value = history.value.length;
  } catch {
    if (reset) {
      history.value = [];
      total.value = 0;
    }
  } finally {
    loading.value = false;
  }
}

async function loadMore() {
  if (!hasMore.value || loadingMore.value) return;
  loadingMore.value = true;
  try {
    await load(false);
  } finally {
    loadingMore.value = false;
  }
}

function close() {
  emit("update:modelValue", false);
}

function formatBytes(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(2)} MB`;
}
</script>

<template>
  <el-drawer :model-value="modelValue" title="路径版本历史" size="460px" @close="close">
    <p class="path-label">{{ path }}</p>
    <p v-if="total > 0" class="meta">共 {{ total }} 条记录，已加载 {{ history.length }}</p>
    <el-table v-loading="loading" :data="history" size="small" stripe empty-text="无历史记录">
      <el-table-column prop="txn_id" label="Txn" width="72" />
      <el-table-column label="大小" width="88" align="right">
        <template #default="{ row }">{{ formatBytes(row.size) }}</template>
      </el-table-column>
      <el-table-column prop="content_hash" label="内容 Hash" min-width="120" show-overflow-tooltip />
      <el-table-column prop="mtime" label="mtime" width="96" />
    </el-table>
    <div v-if="hasMore" class="load-more">
      <el-button size="small" :loading="loadingMore" @click="loadMore">加载更多</el-button>
    </div>
  </el-drawer>
</template>

<style scoped>
.path-label {
  margin: 0 0 8px;
  font-size: 13px;
  color: var(--text-soft);
  word-break: break-all;
}
.meta {
  margin: 0 0 12px;
  font-size: 12px;
  color: var(--text-soft);
}
.load-more {
  margin-top: 12px;
  text-align: center;
}
</style>
