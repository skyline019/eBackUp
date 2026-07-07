<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { SHORTCUT_HELP } from "@/composables/useGlobalShortcuts";
import { HELP_FAQ, HELP_QUICKSTART, ACTIVITY_GUIDES } from "@/utils/helpContent";
import { runtimeInfo, uiSettingsPath } from "@/api/ebbackup";
import { isTauriRuntime } from "@/utils/tauriRuntime";

const ui = useUiStore();

const tab = computed({
  get: () => ui.helpCenterTab,
  set: (v: string) => {
    ui.helpCenterTab = v;
  },
});
const aboutAbi = ref<number | null>(null);
const aboutWorkbench = ref("");
const settingsPath = ref("");

const visible = computed({
  get: () => ui.showHelpCenter,
  set: (v: boolean) => {
    ui.showHelpCenter = v;
  },
});

const currentGuide = computed(() => ACTIVITY_GUIDES[ui.activity]);

onMounted(async () => {
  if (!isTauriRuntime()) return;
  try {
    const [rt, path] = await Promise.all([runtimeInfo(), uiSettingsPath()]);
    aboutAbi.value = rt.abi_version;
    aboutWorkbench.value = rt.workbench;
    settingsPath.value = path;
  } catch {
    /* ignore */
  }
});

function openHelpTab(id: string) {
  ui.helpCenterTab = id;
  ui.showHelpCenter = true;
}

defineExpose({ openHelpTab });
</script>

<template>
  <el-dialog
    v-model="visible"
    title="帮助中心"
    width="640px"
    align-center
    append-to-body
    class="help-center-dialog"
  >
    <el-tabs v-model="tab" class="help-tabs">
      <el-tab-pane label="快速入门" name="quickstart">
        <section v-for="s in HELP_QUICKSTART" :key="s.id" class="help-block">
          <h4>{{ s.title }}</h4>
          <ul>
            <li v-for="(line, i) in s.body" :key="i">{{ line }}</li>
          </ul>
        </section>
      </el-tab-pane>

      <el-tab-pane label="当前活动" name="activity">
        <section class="help-block">
          <h4>{{ currentGuide.title }}</h4>
          <ul>
            <li v-for="(line, i) in currentGuide.body" :key="i">{{ line }}</li>
          </ul>
        </section>
        <p class="help-meta">活动切换：Ctrl+1 … Ctrl+6</p>
      </el-tab-pane>

      <el-tab-pane label="常见问题" name="faq">
        <section v-for="s in HELP_FAQ" :key="s.id" class="help-block">
          <h4>{{ s.title }}</h4>
          <p v-for="(line, i) in s.body" :key="i">{{ line }}</p>
        </section>
      </el-tab-pane>

      <el-tab-pane label="快捷键" name="keys">
        <div class="shortcut-grid">
          <template v-for="(row, i) in SHORTCUT_HELP" :key="i">
            <span>{{ row.desc }}</span>
            <kbd>{{ row.keys }}</kbd>
          </template>
        </div>
      </el-tab-pane>

      <el-tab-pane label="关于" name="about">
        <dl class="about-dl">
          <dt>产品</dt>
          <dd>ebbackup Workbench v0.1.0</dd>
          <dt>内核</dt>
          <dd>ebbackup C++ · ABI v{{ aboutAbi ?? "—" }}</dd>
          <dt>接口</dt>
          <dd>{{ aboutWorkbench || "ebbackup_workbench.dll" }}</dd>
          <dt>设置文件</dt>
          <dd class="mono">{{ settingsPath || (isTauriRuntime() ? "加载中…" : "浏览器 localStorage") }}</dd>
          <dt>文档</dt>
          <dd>仓库 docs/product/WORKBENCH_GUI.md</dd>
        </dl>
      </el-tab-pane>
    </el-tabs>

    <template #footer>
      <el-button @click="visible = false">关闭</el-button>
      <el-button type="primary" @click="tab = 'quickstart'">查看演示流程</el-button>
    </template>
  </el-dialog>
</template>

<style scoped>
.help-tabs {
  margin-top: -8px;
}
.help-block {
  margin-bottom: 16px;
}
.help-block h4 {
  margin: 0 0 8px;
  font-size: 14px;
  color: var(--text-main);
}
.help-block ul {
  margin: 0;
  padding-left: 1.2em;
  font-size: 13px;
  line-height: 1.55;
  color: var(--text-regular);
}
.help-block p {
  margin: 0 0 6px;
  font-size: 13px;
  line-height: 1.55;
  color: var(--text-regular);
}
.help-meta {
  font-size: 12px;
  color: var(--text-soft);
}
.shortcut-grid {
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 10px 20px;
  font-size: 13px;
}
.shortcut-grid kbd {
  font-family: var(--font-mono);
  font-size: 11px;
  padding: 3px 8px;
  border-radius: 4px;
  border: 1px solid var(--shell-line);
  background: color-mix(in srgb, var(--panel-bg-strong) 80%, transparent);
}
.about-dl {
  display: grid;
  grid-template-columns: 88px 1fr;
  gap: 8px 12px;
  font-size: 13px;
}
.about-dl dt {
  color: var(--text-soft);
}
.about-dl dd {
  margin: 0;
  color: var(--text-main);
  word-break: break-all;
}
.about-dl .mono {
  font-family: var(--font-mono);
  font-size: 11px;
}
</style>
