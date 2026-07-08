<script setup lang="ts">
import { ref, watch } from "vue";

const props = defineProps<{
  modelValue: boolean;
}>();

const emit = defineEmits<{
  (e: "update:modelValue", v: boolean): void;
  (e: "unlock", payload: { mode: "password" | "recovery"; value: string }): void;
}>();

const mode = ref<"password" | "recovery">("password");
const password = ref("");
const recoveryKey = ref("");

watch(
  () => props.modelValue,
  (v) => {
    if (v) {
      mode.value = "password";
      password.value = "";
      recoveryKey.value = "";
    }
  }
);

function close() {
  emit("update:modelValue", false);
}

function submit() {
  const value = mode.value === "password" ? password.value.trim() : recoveryKey.value.trim();
  if (!value) return;
  emit("unlock", { mode: mode.value, value });
}
</script>

<template>
  <el-dialog
    :model-value="modelValue"
    title="解锁加密仓库"
    width="460px"
    @update:model-value="emit('update:modelValue', $event)"
  >
    <el-radio-group v-model="mode" class="unlock-mode">
      <el-radio label="password">备份密码</el-radio>
      <el-radio label="recovery">恢复密钥（26 字符）</el-radio>
    </el-radio-group>
    <el-input
      v-if="mode === 'password'"
      v-model="password"
      type="password"
      show-password
      placeholder="仓库加密密码"
    />
    <el-input v-else v-model="recoveryKey" placeholder="恢复密钥" maxlength="26" />
    <template #footer>
      <el-button @click="close">取消</el-button>
      <el-button type="primary" @click="submit">解锁</el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.unlock-mode {
  margin-bottom: 12px;
}
</style>
