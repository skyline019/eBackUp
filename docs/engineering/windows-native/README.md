# Windows 原生元数据专册（LaTeX）

独立于 [`kernel-manual/`](../kernel-manual/) 的 **Windows NTFS / VSS / EFS** 实现专册：每板块一本 PDF。

**叙事方式**：从「仅有 POSIX 语义 / naive 备份」的问题出发，按 Wave / Phase / GAP 演进顺序展开设计逻辑，附 TikZ 图例与 \filepath{engine\_cpp} 源码摘录。

**实现根目录**：`engine_cpp/src/winmeta/`、`engine_cpp/src/scan/scan_entry.cc`、`engine_cpp/src/engine/manifest.cc`

## 板块与 PDF

| PDF | 主题 |
|-----|------|
| `01-manifest-v5.pdf` | EBMANIFEST5 选型、meta flags、二进制 body |
| `02-scan-enrichment.pdf` | 扫描 enrichment 总流程 |
| `03-acl-sd.pdf` | 安全描述符采集与四策略恢复 |
| `04-ads.pdf` | 备用数据流 ADS |
| `05-hardlink-inode.pdf` | inode\_id 与硬链 dedup/恢复 |
| `06-reparse-junction.pdf` | 重解析点 / 目录联接 |
| `07-sparse-ntfs.pdf` | NTFS 稀疏 run 与 restore |
| `08-efs.pdf` | EFS Tier A/B 与密钥 blob |
| `09-vss.pdf` | VSS 会话、多卷闭包、影子存储 |

## 构建（WSL XeLaTeX）

```bash
cd /mnt/e/recoveryProjects/docs/engineering/windows-native
make all
# 或单本：
make 03-acl-sd
```

产物：`build/*.pdf`

依赖：TeX Live + `ctex`、TikZ、`listings`（与 kernel-manual 相同）。
