<script setup lang="ts">
import { computed } from "vue";
import ActivityBar from "@/shell/ActivityBar.vue";
import RibbonBar from "@/shell/RibbonBar.vue";
import OutputPanel from "@/shell/OutputPanel.vue";
import ContextSidebar from "@/shell/ContextSidebar.vue";
import RepoHome from "@/activities/RepoHome.vue";
import BackupView from "@/activities/BackupView.vue";
import SnapshotsView from "@/activities/SnapshotsView.vue";
import BrowseView from "@/activities/BrowseView.vue";
import DiffView from "@/activities/DiffView.vue";
import RestoreView from "@/activities/RestoreView.vue";
import VerifyView from "@/activities/VerifyView.vue";
import MaintenanceView from "@/activities/MaintenanceView.vue";
import { useUiStore } from "@/stores/uiStore";
import { useSidebarResize } from "@/composables/useSidebarResize";
import OpacityRegulator from "@/components/OpacityRegulator.vue";
import TaskFlowBar from "@/components/TaskFlowBar.vue";
import WorkflowJourney from "@/components/WorkflowJourney.vue";

const ui = useUiStore();
const { sidebarResizing, startResize } = useSidebarResize();

const mainView = computed(() => {
  switch (ui.activity) {
    case "repo":
      return RepoHome;
    case "backup":
      return BackupView;
    case "snapshots":
      return SnapshotsView;
    case "browse":
      return BrowseView;
    case "diff":
      return DiffView;
    case "restore":
      return RestoreView;
    case "verify":
      return VerifyView;
    case "maintenance":
      return MaintenanceView;
    default:
      return RepoHome;
  }
});
</script>

<template>
  <div class="workbench-shell" :class="ui.layoutClass" :style="ui.layoutStyle">
    <img
      v-if="ui.settings.bgMode === 'image' && ui.settings.bgImageUrl.trim()"
      class="layout-image-wallpaper"
      :src="ui.settings.bgImageUrl"
      alt=""
    />
    <video
      v-if="ui.settings.bgMode === 'video' && ui.settings.bgVideoUrl.trim()"
      class="layout-video-wallpaper"
      :src="ui.settings.bgVideoUrl"
      autoplay
      muted
      loop
      playsinline
    />

    <ActivityBar />
    <ContextSidebar v-if="ui.showSidebar" class="sidebar" />
    <div
      v-if="ui.showSidebar"
      class="sidebar-resizer"
      :class="{ active: sidebarResizing }"
      title="拖拽调整侧栏宽度"
      @mousedown.prevent="startResize"
    />

    <section class="content workspace" :class="{ 'console-collapsed': ui.settings.logCollapsed }">
      <RibbonBar />
      <WorkflowJourney />
      <TaskFlowBar />
      <main class="workspace-main workspace-opacity-anchor">
        <OpacityRegulator
          class="workspace-opacity-regulator"
          setting-key="workspaceCardOpacity"
          label="卡片"
          compact
        />
        <component :is="mainView" />
      </main>
      <OutputPanel />
    </section>
  </div>
</template>
