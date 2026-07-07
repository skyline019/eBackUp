<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { verifyRepo, recoverRepo, setAuditKey } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";
import { setActivityRunner } from "@/composables/useActivityRunners";
import { confirmDestructive } from "@/utils/confirmDestructive";
import { enrichError, formatVerifyError } from "@/utils/errorMessages";
import FieldTip from "@/components/FieldTip.vue";
import EmptyState from "@/components/EmptyState.vue";

const repo = useRepoStore();
const ui = useUiStore();

const txnId = ref<number | undefined>(undefined);
const requireAnchor = ref(false);
const auditKey = ref("");
const busy = ref(false);

const FLAG_REQUIRE_ANCHOR = 0x0004;

const anchorDisabled = computed(() => (txnId.value ?? 0) > 0);

let unregVerify: (() => void) | null = null;
let unregRecover: (() => void) | null = null;

onMounted(() => {
  unregVerify = setActivityRunner("verify-run", verify);
  unregRecover = setActivityRunner("recover-run", recover);
});

onUnmounted(() => {
  unregVerify?.();
  unregRecover?.();
});

watch(anchorDisabled, (disabled) => {
  if (disabled && requireAnchor.value) {
    requireAnchor.value = false;
    ui.pushLog("指定 Txn 验证时不支持强制锚点，已自动关闭", "meta");
  }
});

async function verify() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  if (requireAnchor.value && !auditKey.value.trim()) {
    ui.pushLog("开启强制锚点验证时，请填写审计密钥", "error");
    return;
  }
  busy.value = true;
  try {
    await setAuditKey(auditKey.value.trim());
    const flags = requireAnchor.value ? FLAG_REQUIRE_ANCHOR : 0;
    const res = await verifyRepo(txnId.value || undefined, flags);
    ui.setAuditResult(res);
    ui.pushLog("验证通过", "success");
  } catch (e) {
    ui.pushLog(formatVerifyError(await enrichError(e)), "error");
  } finally {
    busy.value = false;
  }
}

async function recover() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  const ok = await confirmDestructive(
    "修复中断事务",
    "将把未完成的事务标记为 aborted，便于继续增量备份。请确认无正在进行的备份。"
  );
  if (!ok) return;
  busy.value = true;
  try {
    const res = await recoverRepo();
    ui.setAuditResult(res);
    ui.pushLog("中断事务已修复", "success");
    await repo.refreshInfo();
    await repo.refreshSnapshots();
  } catch (e) {
    ui.pushLog(formatVerifyError(await enrichError(e)), "error");
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <div class="activity-page">
    <section class="panel-card">
      <div class="head-row">
        <h2>完整性验证</h2>
        <el-button link type="primary" @click="ui.openHelp('activity')">本页帮助</el-button>
      </div>
      <EmptyState
        v-if="!repo.isOpen"
        title="请先打开仓库"
        action-label="前往仓库页"
        @action="ui.setActivity('repo')"
      />
      <el-form v-else label-width="120px">
        <el-form-item label="快照 Txn">
          <el-input-number v-model="txnId" :min="0" placeholder="留空=最新" />
          <FieldTip content="留空验证最新 manifest；填写数字验证历史快照。" />
        </el-form-item>
        <el-form-item label="审计密钥">
          <el-input
            v-model="auditKey"
            type="password"
            show-password
            placeholder="CARL 锚点签名密钥（强制锚点时必填）"
            clearable
          />
          <FieldTip content="与发布锚点时使用的密钥一致；仅保存在当前会话，不写入磁盘。" />
        </el-form-item>
        <el-form-item label="强制锚点">
          <el-switch v-model="requireAnchor" :disabled="anchorDisabled" />
          <p class="field-hint">
            关闭（默认）：manifest + chunk 深度校验，适合答辩演示。
            开启：额外校验 CARL 锚点签名，须填写上方审计密钥。
          </p>
        </el-form-item>
        <el-alert
          v-if="anchorDisabled"
          type="info"
          :closable="false"
          show-icon
          title="指定 Txn 时仅做快照级校验"
          class="anchor-alert"
        />
        <el-alert
          v-else-if="requireAnchor && !auditKey.trim()"
          type="warning"
          :closable="false"
          show-icon
          title="请填写审计密钥"
          description="开启强制锚点后，需在上方输入密钥再点「验证仓库」。"
          class="anchor-alert"
        />
        <el-form-item>
          <el-button type="primary" :loading="busy" @click="verify">验证仓库</el-button>
        </el-form-item>
      </el-form>
    </section>

    <section class="panel-card">
      <h2>修复中断备份</h2>
      <p class="desc">将未完成的事务标记为 aborted，便于继续增量备份。</p>
      <el-button type="warning" :loading="busy" :disabled="!repo.isOpen" @click="recover">
        Recover
      </el-button>
    </section>
  </div>
</template>

<style scoped>
.activity-page {
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  overflow: auto;
  height: 100%;
}
.head-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}
.head-row h2,
.panel-card h2 {
  margin: 0;
  font-size: 15px;
}
.desc {
  font-size: 13px;
  color: var(--text-soft);
  margin: 0 0 12px;
}
.field-hint {
  margin: 6px 0 0;
  font-size: 12px;
  line-height: 1.5;
  color: var(--text-soft);
  max-width: 520px;
}
.anchor-alert {
  margin-bottom: 12px;
  max-width: 560px;
}
</style>
