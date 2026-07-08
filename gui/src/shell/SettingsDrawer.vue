<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { useUiStore } from "@/stores/uiStore";
import { testWebhook } from "@/api/ebbackup";
import { useProfileStore } from "@/stores/profileStore";
import { deleteProfile, renameProfile } from "@/api/ebbackup";
import { isTauriRuntime } from "@/utils/tauriRuntime";
import {
  defaultUiSettings,
  hasActiveWallpaper,
  OPACITY_LIMITS,
  OPACITY_FULL,
  settingsPresets,
  WALLPAPER_READABILITY_FLOORS,
} from "@/utils/themeSettings";
import { useWallpaperFiles } from "@/composables/useWallpaperFiles";
import { ElMessage, ElMessageBox } from "element-plus";

const props = defineProps<{ modelValue: boolean }>();
const emit = defineEmits<{ (e: "update:modelValue", v: boolean): void }>();

const ui = useUiStore();
const profile = useProfileStore();
const tab = ref<"theme" | "wallpaper" | "layout" | "profile">("theme");

const visible = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit("update:modelValue", v),
});

const {
  bgImageInput,
  bgVideoInput,
  pickBackgroundImage,
  pickBackgroundVideo,
  onBackgroundImageChange,
  onBackgroundVideoChange,
  clearWallpaper,
} = useWallpaperFiles();

watch(visible, (v) => {
  if (v) ui.applyThemeVars();
});

function onPreview() {
  ui.applyThemeVars();
  ui.schedulePersist();
}

function applyPresetKey(key: string) {
  const p = settingsPresets.find((x) => x.key === key);
  if (!p) return;
  ui.applyPreset(p.settings);
  ElMessage.success(`已应用：${p.label}`);
}

function resetColors() {
  ui.applyPreset({
    pageBg: defaultUiSettings.pageBg,
    tableBg: defaultUiSettings.tableBg,
    textMain: defaultUiSettings.textMain,
    textRegular: defaultUiSettings.textRegular,
    textSoft: defaultUiSettings.textSoft,
    accent: defaultUiSettings.accent,
  });
}

const wallpaperActive = computed(() => hasActiveWallpaper(ui.settings));

function applyGlass() {
  const f = WALLPAPER_READABILITY_FLOORS;
  ui.applyPreset({
    panelOpacity: f.panel,
    workspaceCardOpacity: f.workspace,
    tableViewOpacity: f.tableView,
    logPanelOpacity: f.logPanel,
  });
  ElMessage.info("已应用壁纸可读模式（主视图透明度不低于阈值）");
}

async function onSave() {
  await ui.persistSettingsNow();
  ui.applyThemeVars();
  const where = ui.settingsFilePath || "localStorage";
  ui.pushLog(`外观设置已保存 (${where})`, "success");
  visible.value = false;
}

async function onTestWebhook() {
  const url = ui.settings.defaultWebhookUrl?.trim();
  if (!url) return;
  try {
    await testWebhook(url);
    ElMessage.success("Webhook 测试成功");
  } catch (e) {
    ElMessage.error(String(e));
  }
}

async function onRenameProfile(p: { id: string; name: string }) {
  if (!isTauriRuntime()) return;
  try {
    const { value } = await ElMessageBox.prompt("新名称", "重命名 Profile", {
      inputValue: p.name,
      confirmButtonText: "保存",
      cancelButtonText: "取消",
    });
    if (!value?.trim() || value.trim() === p.name) return;
    await renameProfile(p.id, value.trim());
    await profile.refresh();
    ElMessage.success("已重命名");
  } catch {
    /* cancelled */
  }
}

async function onDeleteProfile(p: { id: string; name: string }) {
  if (!isTauriRuntime() || p.id === "default") return;
  try {
    await ElMessageBox.confirm(`删除 Profile「${p.name}」？`, "确认", { type: "warning" });
    await deleteProfile(p.id);
    await profile.refresh();
    if (profile.activeProfileId === p.id) {
      await profile.switchTo("default");
    }
    ElMessage.success("已删除");
  } catch {
    /* cancelled */
  }
}
</script>

<template>
  <el-drawer
    v-model="visible"
    title="外观与布局"
    size="420px"
    class="settings-modal"
    append-to-body
    :z-index="28001"
    @opened="onPreview"
  >
    <el-tabs v-model="tab" class="settings-tabs">
      <el-tab-pane label="主题" name="theme">
        <el-form label-position="top" size="small" @submit.prevent>
          <el-form-item label="预设">
            <el-select placeholder="选择预设" style="width: 100%" @change="applyPresetKey">
              <el-option
                v-for="p in settingsPresets"
                :key="p.key"
                :label="p.label"
                :value="p.key"
              />
            </el-select>
          </el-form-item>
          <el-form-item label="强调色">
            <el-color-picker v-model="ui.settings.accent" @change="onPreview" />
          </el-form-item>
          <el-form-item label="主文字色">
            <el-color-picker v-model="ui.settings.textMain" @change="onPreview" />
          </el-form-item>
          <el-form-item label="常规文字色">
            <el-color-picker v-model="ui.settings.textRegular" @change="onPreview" />
          </el-form-item>
          <el-form-item label="次要文字色">
            <el-color-picker v-model="ui.settings.textSoft" @change="onPreview" />
          </el-form-item>
          <el-form-item label="页面底色">
            <el-color-picker v-model="ui.settings.pageBg" @change="onPreview" />
          </el-form-item>
          <el-form-item label="表格底色">
            <el-color-picker v-model="ui.settings.tableBg" @change="onPreview" />
          </el-form-item>
          <div class="row-btns">
            <el-button size="small" @click="resetColors">重置配色</el-button>
            <el-button size="small" @click="applyGlass">壁纸可读</el-button>
          </div>
        </el-form>
      </el-tab-pane>

      <el-tab-pane label="壁纸" name="wallpaper">
        <el-form label-position="top" size="small">
          <el-form-item label="背景模式">
            <el-radio-group v-model="ui.settings.bgMode" @change="onPreview">
              <el-radio value="gradient">渐变</el-radio>
              <el-radio value="image">图片</el-radio>
              <el-radio value="video">视频</el-radio>
            </el-radio-group>
          </el-form-item>
          <template v-if="ui.settings.bgMode === 'image'">
            <el-form-item label="图片 URL">
              <el-input
                v-model="ui.settings.bgImageUrl"
                placeholder="https://... 或选择本地文件"
                @input="onPreview"
              />
            </el-form-item>
            <el-button size="small" @click="pickBackgroundImage">选择本地图片</el-button>
            <input
              ref="bgImageInput"
              type="file"
              accept="image/*"
              hidden
              @change="onBackgroundImageChange"
            />
            <el-form-item label="图片不透明度" style="margin-top: 12px">
              <el-slider
                v-model="ui.settings.bgImageOpacity"
                :min="0.05"
                :max="0.8"
                :step="0.02"
                @input="onPreview"
              />
            </el-form-item>
          </template>
          <template v-if="ui.settings.bgMode === 'video'">
            <el-form-item label="视频 URL">
              <el-input v-model="ui.settings.bgVideoUrl" @input="onPreview" />
            </el-form-item>
            <el-button size="small" @click="pickBackgroundVideo">选择本地视频</el-button>
            <input
              ref="bgVideoInput"
              type="file"
              accept="video/*"
              hidden
              @change="onBackgroundVideoChange"
            />
            <el-form-item label="视频不透明度" style="margin-top: 12px">
              <el-slider
                v-model="ui.settings.bgVideoOpacity"
                :min="0.05"
                :max="0.8"
                :step="0.02"
                @input="onPreview"
              />
            </el-form-item>
          </template>
          <el-button size="small" type="danger" plain @click="clearWallpaper">
            清除壁纸
          </el-button>
        </el-form>
      </el-tab-pane>

      <el-tab-pane label="布局" name="layout">
        <p v-if="wallpaperActive" class="wallpaper-opacity-hint">
          壁纸已开启：表格与侧栏面板不低于
          {{ Math.round(WALLPAPER_READABILITY_FLOORS.panel * 100) }}% /
          主视图 {{ Math.round(WALLPAPER_READABILITY_FLOORS.workspace * 100) }}% /
          边框 {{ Math.round(WALLPAPER_READABILITY_FLOORS.chrome * 100) }}%。
          工作区卡片与输出面板可在 5%–100% 全范围调节。
        </p>
        <el-form label-position="top" size="small">
          <p class="opacity-section-title">主工作区卡片 / 输出面板（全范围 5%–100%）</p>
          <el-form-item label="工作区卡片不透明度">
            <el-slider
              v-model="ui.settings.workspaceCardOpacity"
              :min="OPACITY_FULL.min"
              :max="OPACITY_FULL.max"
              :step="0.01"
              :format-tooltip="(v: number) => `${Math.round(v * 100)}%`"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="输出面板不透明度">
            <el-slider
              v-model="ui.settings.logPanelOpacity"
              :min="OPACITY_FULL.min"
              :max="OPACITY_FULL.max"
              :step="0.01"
              :format-tooltip="(v: number) => `${Math.round(v * 100)}%`"
              @input="onPreview"
            />
          </el-form-item>
          <el-divider />
          <el-form-item label="侧栏宽度">
            <el-slider
              v-model="ui.settings.sidebarWidth"
              :min="200"
              :max="460"
              :step="10"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="面板不透明度">
            <el-slider
              v-model="ui.settings.panelOpacity"
              :min="OPACITY_LIMITS.panel.min"
              :max="OPACITY_LIMITS.panel.max"
              :step="0.02"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="表格区不透明度">
            <el-slider
              v-model="ui.settings.tableViewOpacity"
              :min="OPACITY_LIMITS.tableView.min"
              :max="OPACITY_LIMITS.tableView.max"
              :step="0.02"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="界面字体缩放">
            <el-slider
              v-model="ui.settings.fontScale"
              :min="0.85"
              :max="1.25"
              :step="0.05"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="日志字体缩放">
            <el-slider
              v-model="ui.settings.logFontScale"
              :min="0.88"
              :max="1.25"
              :step="0.02"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="圆角缩放">
            <el-slider
              v-model="ui.settings.cornerScale"
              :min="0.8"
              :max="1.35"
              :step="0.05"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="阴影强度">
            <el-slider
              v-model="ui.settings.shadowScale"
              :min="0.6"
              :max="1.5"
              :step="0.05"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="紧凑模式">
            <el-switch v-model="ui.settings.denseMode" @change="onPreview" />
          </el-form-item>
          <el-form-item label="界面动画">
            <el-switch v-model="ui.settings.animations" @change="onPreview" />
          </el-form-item>
          <el-form-item label="默认折叠输出面板">
            <el-switch v-model="ui.settings.logCollapsed" @change="onPreview" />
          </el-form-item>
          <el-form-item label="备份滞后告警（天）">
            <el-input-number
              v-model="ui.settings.staleBackupAlertDays"
              :min="1"
              :max="365"
              @change="onPreview"
            />
          </el-form-item>
          <el-form-item label="同步滞后告警（天）">
            <el-input-number
              v-model="ui.settings.staleSyncAlertDays"
              :min="1"
              :max="365"
              @change="onPreview"
            />
          </el-form-item>
          <el-form-item label="默认 Webhook URL">
            <el-input
              v-model="ui.settings.defaultWebhookUrl"
              placeholder="新建作业时预填"
              @input="onPreview"
            />
          </el-form-item>
          <el-form-item label="Webhook 测试">
            <el-button
              size="small"
              :disabled="!ui.settings.defaultWebhookUrl?.trim()"
              @click="onTestWebhook"
            >
              发送测试 POST
            </el-button>
          </el-form-item>
        </el-form>
      </el-tab-pane>

      <el-tab-pane v-if="isTauriRuntime()" label="Profile" name="profile">
        <p class="wallpaper-opacity-hint">
          每个 Profile 独立保存主题、壁纸、最近仓库与备份滞后告警；切换时自动关闭当前仓库。
        </p>
        <el-table :data="profile.profiles" size="small" style="width: 100%">
          <el-table-column prop="name" label="名称" />
          <el-table-column label="操作" width="140">
            <template #default="{ row }">
              <el-button link type="primary" size="small" @click="onRenameProfile(row)">重命名</el-button>
              <el-button
                link
                type="danger"
                size="small"
                :disabled="row.id === 'default' || row.id === profile.activeProfileId"
                @click="onDeleteProfile(row)"
              >
                删除
              </el-button>
            </template>
          </el-table-column>
        </el-table>
      </el-tab-pane>
    </el-tabs>

    <div class="drawer-footer">
      <el-button @click="visible = false">取消</el-button>
      <el-button type="primary" @click="onSave">保存</el-button>
    </div>
  </el-drawer>
</template>

<style scoped>
.row-btns {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
  margin-top: 8px;
}
.drawer-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  padding-top: 12px;
  border-top: 1px solid var(--shell-line);
  margin-top: 8px;
}
.wallpaper-opacity-hint {
  margin: 0 0 12px;
  padding: 8px 10px;
  font-size: 11px;
  line-height: 1.45;
  color: var(--text-soft);
  border-radius: 8px;
  border: 1px solid color-mix(in srgb, var(--accent) 25%, var(--card-border));
  background: color-mix(in srgb, var(--accent) 8%, var(--panel-bg-input));
}
.opacity-section-title {
  margin: 0 0 8px;
  font-size: 12px;
  font-weight: 600;
  color: var(--text-regular);
}
</style>
