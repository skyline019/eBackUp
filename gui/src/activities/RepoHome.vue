<script setup lang="ts">
import { onMounted, ref } from "vue";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { pickDirectory } from "@/api/ebbackup";
import { enrichError, formatGenericError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";

const repo = useRepoStore();
const ui = useUiStore();

const parentDir = ref("E:\\data");
const repoName = ref("mybackup");
const openPath = ref("");
const initLegacy = ref(false);

const FLAG_INIT_LEGACY = 0x0200;

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
    const flags = initLegacy.value ? FLAG_INIT_LEGACY : 0;
    await repo.createRepo(parentDir.value, repoName.value, flags);
  } catch (e) {
    ui.pushLog(formatGenericError(await enrichError(e), "创建失败"), "error");
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
    ui.pushLog(formatGenericError(e), "error");
  }
}

async function openRecent(path: string) {
  try {
    await repo.open(path);
  } catch (e) {
    ui.pushLog(formatGenericError(e), "error");
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
      <div class="head-row">
        <h2>新建仓库</h2>
        <el-button link type="primary" @click="ui.openHelp('quickstart')">演示流程</el-button>
      </div>
      <el-form label-width="110px" @submit.prevent="createRepo">
        <el-form-item label="父目录">
          <div class="row">
            <el-input v-model="parentDir" />
            <el-button @click="browseParent">浏览</el-button>
          </div>
        </el-form-item>
        <el-form-item label="名称">
          <el-input v-model="repoName" placeholder="mybackup" />
        </el-form-item>
        <el-form-item label="兼容">
          <el-checkbox v-model="initLegacy">v0.3 旧版布局</el-checkbox>
          <FieldTip content="仅在与旧仓库格式对接时开启；新课程项目保持关闭。" />
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
      <el-form label-width="110px" @submit.prevent="openExisting">
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

    <section v-if="repo.recent.length" class="panel-card">
      <h2>最近仓库</h2>
      <ul class="recent-list">
        <li v-for="p in repo.recent" :key="p">
          <button type="button" class="recent-btn" @click="openRecent(p)">{{ p }}</button>
        </li>
      </ul>
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
      <el-button type="primary" link @click="ui.setActivity('backup')">前往备份 →</el-button>
    </section>
  </div>
</template>

<style scoped>
.activity-page.repo-home {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 16px 20px;
  overflow: auto;
  height: 100%;
}
.head-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}
.head-row h2,
.panel-card h2 {
  margin: 0;
  font-size: 15px;
}
.row {
  display: flex;
  gap: 8px;
  width: 100%;
}
.recent-list {
  list-style: none;
  margin: 0;
  padding: 0;
}
.recent-btn {
  width: 100%;
  text-align: left;
  padding: 8px 10px;
  margin-bottom: 4px;
  border: none;
  border-radius: 6px;
  background: var(--hover-bg);
  color: var(--text-regular);
  cursor: pointer;
  font-size: 12px;
  word-break: break-all;
}
.recent-btn:hover {
  background: color-mix(in srgb, var(--accent) 12%, var(--hover-bg));
}
.summary dl {
  display: grid;
  grid-template-columns: 120px 1fr;
  gap: 8px;
  font-size: 13px;
  margin-bottom: 12px;
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
