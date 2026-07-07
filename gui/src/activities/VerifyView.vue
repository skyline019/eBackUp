<script setup lang="ts">
import { ref } from "vue";
import { verifyRepo, recoverRepo } from "@/api/ebbackup";
import { useRepoStore } from "@/stores/repoStore";
import { useUiStore } from "@/stores/uiStore";

const repo = useRepoStore();
const ui = useUiStore();

const txnId = ref<number | undefined>(undefined);
const requireAnchor = ref(false);
const busy = ref(false);

const FLAG_REQUIRE_ANCHOR = 0x0004;

async function verify() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  busy.value = true;
  try {
    const flags = requireAnchor.value ? FLAG_REQUIRE_ANCHOR : 0;
    const res = await verifyRepo(txnId.value || undefined, flags);
    ui.setAuditResult(res);
    ui.pushLog("验证通过", "success");
  } catch (e) {
    ui.pushLog(String(e), "error");
  } finally {
    busy.value = false;
  }
}

async function recover() {
  if (!repo.isOpen) {
    ui.pushLog("请先打开仓库", "error");
    return;
  }
  busy.value = true;
  try {
    const res = await recoverRepo();
    ui.setAuditResult(res);
    ui.pushLog("中断事务已修复", "success");
  } catch (e) {
    ui.pushLog(String(e), "error");
  } finally {
    busy.value = false;
  }
}
</script>

<template>
  <div class="activity-page">
    <section class="panel-card">
      <h2>完整性验证</h2>
      <el-form label-width="120px">
        <el-form-item label="快照 Txn">
          <el-input-number v-model="txnId" :min="0" placeholder="留空=最新" />
        </el-form-item>
        <el-form-item label="Require anchor">
          <el-switch v-model="requireAnchor" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="busy" :disabled="!repo.isOpen" @click="verify">
            验证仓库
          </el-button>
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
.desc {
  font-size: 13px;
  color: var(--text-soft);
  margin: 0 0 12px;
}
</style>
