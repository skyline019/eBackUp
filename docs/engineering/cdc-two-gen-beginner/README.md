# 两代 CDC 算法 — 小白完全手册

面向**只有基本代码基础**的读者，用大量图示一步一步讲清：

1. **第一代**：FastCDC Stream（ebbackup 默认切法）
2. **第二代**：G-TCDC v6 2F-Gear（opt-in 新切法）

```bash
cd /mnt/e/recoveryProjects/docs/engineering/cdc-two-gen-beginner
make pdf   # WSL xelatex => build/main.pdf（约 56 页，每章含逐步模拟示例）
```

进阶工程手册见 [`gtcdc-v6-manual/`](../gtcdc-v6-manual/)、[`kernel-manual/`](../kernel-manual/)。
