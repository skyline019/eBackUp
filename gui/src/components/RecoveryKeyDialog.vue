<script setup lang="ts">
import { ref, watch } from "vue";
import { ElMessage } from "element-plus";

const props = defineProps<{
  modelValue: boolean;
  recoveryKey: string;
  title?: string;
}>();

const emit = defineEmits<{
  (e: "update:modelValue", v: boolean): void;
  (e: "confirmed"): void;
}>();

const acknowledged = ref(false);

watch(
  () => props.modelValue,
  (v) => {
    if (v) acknowledged.value = false;
  }
);

function close() {
  emit("update:modelValue", false);
}

async function copyKey() {
  try {
    await navigator.clipboard.writeText(props.recoveryKey);
    ElMessage.success("已复制恢复密钥");
  } catch {
    ElMessage.error("复制失败，请手动保存");
  }
}

function downloadKey() {
  const blob = new Blob([props.recoveryKey + "\n"], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "ebbackup-recovery-key.txt";
  a.click();
  URL.revokeObjectURL(url);
}

function confirm() {
  emit("confirmed");
  close();
}
</script>

<template>
  <el-dialog
    :model-value="modelValue"
    :title="title ?? '保存恢复密钥'"
    width="520px"
    :close-on-click-modal="false"
    :show-close="false"
    @update:model-value="emit('update:modelValue', $event)"
  >
    <el-alert
      type="warning"
      show-icon
      :closable="false"
      title="恢复密钥仅显示一次。丢失后只能通过旧密码访问；请立即离线保存。"
      class="rk-alert"
    />
    <div class="rk-key">{{ recoveryKey }}</div>
    <div class="rk-actions">
      <el-button @click="copyKey">复制</el-button>
      <el-button @click="downloadKey">导出文件</el-button>
    </div>
    <el-checkbox v-model="acknowledged">我已安全保存恢复密钥</el-checkbox>
    <template #footer>
      <el-button type="primary" :disabled="!acknowledged" @click="confirm">继续</el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.rk-alert {
  margin-bottom: 12px;
}
.rk-key {
  font-family: var(--mono-font, monospace);
  font-size: 18px;
  letter-spacing: 0.08em;
  padding: 12px 14px;
  border-radius: 8px;
  background: var(--hover-bg);
  word-break: break-all;
  margin-bottom: 12px;
}
.rk-actions {
  display: flex;
  gap: 8px;
  margin-bottom: 12px;
}
</style>
