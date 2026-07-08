<script setup lang="ts">
import { computed, ref } from "vue";
import { useProfileStore } from "@/stores/profileStore";
import { useUiStore } from "@/stores/uiStore";
import { isTauriRuntime } from "@/utils/tauriRuntime";
import { ElMessage, ElMessageBox } from "element-plus";

const profile = useProfileStore();
const ui = useUiStore();
const busy = ref(false);

const activeName = computed(
  () => profile.activeProfile?.name ?? profile.activeProfileId
);

async function onSwitch(id: string) {
  if (id === profile.activeProfileId || busy.value) return;
  busy.value = true;
  try {
    await profile.switchTo(id);
    ui.pushLog(`已切换 Profile：${profile.activeProfile?.name ?? id}`, "meta");
  } catch (e) {
    ui.pushLog(String(e), "error");
  } finally {
    busy.value = false;
  }
}

async function onCreate() {
  if (!isTauriRuntime()) return;
  try {
    const { value } = await ElMessageBox.prompt("输入新 Profile 名称", "新建 Profile", {
      confirmButtonText: "创建",
      cancelButtonText: "取消",
    });
    if (!value?.trim()) return;
    busy.value = true;
    await profile.create(value.trim());
    ElMessage.success("Profile 已创建");
  } catch {
    /* cancelled */
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <el-dropdown v-if="isTauriRuntime()" trigger="click" @command="onSwitch">
    <button type="button" class="profile-switcher" :disabled="busy">
      {{ activeName }}
    </button>
    <template #dropdown>
      <el-dropdown-menu>
        <el-dropdown-item
          v-for="p in profile.profiles"
          :key="p.id"
          :command="p.id"
          :disabled="p.id === profile.activeProfileId"
        >
          {{ p.name }}
        </el-dropdown-item>
        <el-dropdown-item divided disabled>—</el-dropdown-item>
        <el-dropdown-item @click="onCreate">新建 Profile…</el-dropdown-item>
      </el-dropdown-menu>
    </template>
  </el-dropdown>
</template>

<style scoped>
.profile-switcher {
  border: 1px solid var(--border-subtle, rgba(255, 255, 255, 0.12));
  background: var(--hover-bg, rgba(255, 255, 255, 0.06));
  color: var(--text-regular, #c7d2fe);
  border-radius: 6px;
  padding: 4px 10px;
  font-size: 12px;
  cursor: pointer;
}
.profile-switcher:disabled {
  opacity: 0.6;
  cursor: wait;
}
</style>
