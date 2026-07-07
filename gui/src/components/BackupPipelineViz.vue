<script setup lang="ts">
/** 备份主路径拓扑 — 与内核顺序图一致 */
const nodes = [
  { id: "scan", label: "Scan", sub: "遍历 + Filter" },
  { id: "chunk", label: "Chunk", sub: "FastCDC + SHA256" },
  { id: "encode", label: "Encode", sub: "LZ4 / zstd" },
  { id: "store", label: "Store", sub: "EbPack + idx" },
  { id: "commit", label: "Commit", sub: "manifest + fsync" },
];

defineProps<{
  activeId?: string;
}>();
</script>

<template>
  <div class="pipeline-viz">
    <template v-for="(node, i) in nodes" :key="node.id">
      <div
        class="pv-node"
        :class="{ 'is-active': activeId === node.id, 'is-lit': activeId && nodes.findIndex((n) => n.id === activeId) >= i }"
      >
        <div class="pv-title">{{ node.label }}</div>
        <div class="pv-sub">{{ node.sub }}</div>
      </div>
      <div v-if="i < nodes.length - 1" class="pv-arrow">
        <span class="pv-flow" />
      </div>
    </template>
  </div>
</template>

<style scoped>
.pipeline-viz {
  display: flex;
  align-items: stretch;
  gap: 0;
  padding: 12px 0 4px;
  overflow-x: auto;
}
.pv-node {
  flex: 1;
  min-width: 72px;
  padding: 10px 8px;
  border-radius: 8px;
  border: 1px solid var(--shell-line);
  background: color-mix(in srgb, var(--panel-bg-strong) 65%, transparent);
  text-align: center;
  transition: border-color 0.2s, background 0.2s, transform 0.2s;
}
.pv-node.is-lit {
  border-color: color-mix(in srgb, var(--accent) 40%, var(--shell-line));
}
.pv-node.is-active {
  border-color: var(--accent);
  background: color-mix(in srgb, var(--accent) 14%, transparent);
  transform: translateY(-2px);
  box-shadow: 0 6px 20px color-mix(in srgb, var(--accent) 18%, transparent);
}
.pv-title {
  font-size: 12px;
  font-weight: 700;
  color: var(--text-main);
}
.pv-sub {
  margin-top: 4px;
  font-size: 9px;
  color: var(--text-soft);
  line-height: 1.3;
}
.pv-arrow {
  display: flex;
  align-items: center;
  width: 20px;
  flex-shrink: 0;
}
.pv-flow {
  display: block;
  width: 100%;
  height: 2px;
  background: linear-gradient(90deg, var(--shell-line), var(--accent), var(--shell-line));
  background-size: 200% 100%;
  animation: flow-line 1.2s linear infinite;
}
.pv-node.is-lit + .pv-arrow .pv-flow {
  opacity: 1;
}
@keyframes flow-line {
  0% {
    background-position: 100% 0;
  }
  100% {
    background-position: -100% 0;
  }
}
</style>
