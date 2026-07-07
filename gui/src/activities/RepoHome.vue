<script setup lang="ts">
import { onMounted, ref } from "vue";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { pickDirectory } from "@/api/ebbackup";

const repo = useRepoStore();
const ui = useUiStore();

const parentDir = ref("E:\\data");
const repoName = ref("mybackup");
const openPath = ref("");

onMounted(() => {
  repo.loadLocal();
  if (repo.recent[0]) openPath.value = repo.recent[0];
});

async function browseParent() {
  const p = await pickDirectory();
  if (p) parentDir.value = p;
}

async function browseOpen() {
  const p = await pickDirectory();
  if (p) openPath.value = p;
}

async function createRepo() {
  try {
    await repo.createRepo(parentDir.value, repoName.value);
  } catch (e) {
    ui.pushLog(String(e), "error");
  }
}

async function openExisting() {
  if (!openPath.value.trim()) {
    ui.pushLog("请输入仓库路径", "error");
    return;
  }
  try {
    await repo.open(openPath.value.trim());
  } catch (e) {
    ui.pushLog(String(e), "error");
  }
}

async function closeRepo() {
  try {
    await repo.close();
  } catch (e) {
    ui.pushLog(String(e), "error");
  }
}

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${(n / 1024 / 1024).toFixed(1)} MiB`;
}
</script>

<template>
  <div class="activity-page repo-home">
    <section class="panel-card">
      <h2>新建仓库</h2>
      <el-form label-width="100px" @submit.prevent="createRepo">
        <el-form-item label="父目录">
          <div class="row">
            <el-input v-model="parentDir" />
            <el-button @click="browseParent">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="名称">
          <el-input v-model="repoName" placeholder="mybackup" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="repo.busy" @click="createRepo">
            创建并打开
          </el-button>
        </el-form-item>
      </el-form>
    </section>

    <section class="panel-card">
      <h2>打开已有仓库</h2>
      <el-form label-width="100px" @submit.prevent="openExisting">
        <el-form-item label="路径">
          <div class="row">
            <el-input v-model="openPath" />
            <el-button @click="browseOpen">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="repo.busy" @click="openExisting">打开</el-button>
          <el-button v-if="repo.isOpen" @click="closeRepo">关闭</el-button>
        </el-form-item>
      </el-form>
    </section>

    <section v-if="repo.info" class="panel-card summary">
      <h2>仓库摘要</h2>
      <dl>
        <dt>路径</dt><dd>{{ repo.path }}</dd>
        <dt>ABI</dt><dd>v{{ repo.info.abi_version }}</dd>
        <dt>物理占用</dt><dd>{{ formatBytes(repo.info.physical_bytes) }}</dd>
        <dt>有效数据</dt><dd>{{ formatBytes(repo.info.live_bytes) }}</dd>
        <dt>快照数</dt><dd>{{ repo.snapshots.length }}</dd>
      </dl>
    </section>
  </div>
</template>

<style scoped>
.activity-page.repo-home {
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
.summary dl {
  display: grid;
  grid-template-columns: 120px 1fr;
  gap: 8px;
  font-size: 13px;
}
.summary dt {
  color: var(--text-soft);
}
.summary dd {
  margin: 0;
  color: var(--text-main);
  word-break: break-all;
}
</style>
