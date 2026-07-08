# 压缩与 Zstd 字典

ebbackup 在 chunk 写入路径使用 **ContentClass** 路由（LZ4 / Zstd / Raw），并支持三档 **CompressTier** 与可选的 **仓库级 Zstd 字典**。

---

## CompressTier 档位

| 档位 | CLI / daemon | 典型场景 | Zstd level（快/慢类） | LDM | 默认字典 |
|------|----------------|----------|------------------------|-----|----------|
| `fast` | `--compress-tier fast`（默认） | 日常增量、低 CPU | 1 / 1 | 关 | 否 |
| `balanced` | `--compress-tier balanced` | 容量与速度平衡 | 3 / 6 | 开（≥128 KiB） | 是 |
| `max` | `--compress-tier max` | 归档、冷数据 | 6 / 15 | 开（≥64 KiB） | 是 |

- **`--compress-level N`**：覆盖档位内 Zstd level（0 表示使用档位默认）。
- **`--zstd-dict`**：显式启用字典；**`--no-zstd-dict`** 禁用（即使为 balanced/max）。
- **`balanced` / `max`** 若未指定 `--no-zstd-dict`，会自动启用字典训练与加载。

---

## 仓库字典

训练完成后字典保存在：

```text
{repo}/meta/zstd_dict.bin
```

格式：`EBZD` magic + version + payload（Zstd 训练字典）。

备份结束时会根据本次可压缩样本尝试增量训练并落盘；下次 `Open` 时自动加载。字典通过 `CDict` / `DDict` 缓存，线程安全。

---

## CLI 示例

```powershell
# 平衡档 + 字典（推荐容量敏感场景）
eb backup D:\docs E:\repo --compress-tier balanced

# 最大压缩，自定义 level
eb backup D:\archive E:\repo --compress-tier max --compress-level 12

# 快速档，强制不用字典
eb backup D:\hot E:\repo --compress-tier fast --no-zstd-dict

# 查看 live 压缩率（v0.9.5+）
eb repo-stats E:\repo
```

`eb repo-stats` 与 C API `eb_backup_repo_stats` 输出字段：

| 字段 | 含义 |
|------|------|
| `live_uncompressed_bytes` | 被 manifest 引用的 chunk 逻辑未压缩字节 |
| `live_stored_payload_bytes` | 同上 chunk 的存储 payload 字节（不含 header/pack 开销） |
| `compress_ratio` | `stored / uncompressed`（越小压缩越好；无数据时为 1.0） |
| `compressed_chunk_count` / `raw_chunk_count` | 压缩 vs 原样 chunk 数 |
| `has_zstd_dict` / `zstd_dict_bytes` | 仓库字典是否存在及大小 |

Workbench `repo_info_json` 的 `repo_stats` 对象包含相同字段。

---

## Daemon 配置

`backup_daemon` JSON 配置项：

```json
{
  "compress_tier": "balanced",
  "compress_level": 0,
  "zstd_dict": true
}
```

---

## 相关文档

- [ENVIRONMENT_VARIABLES.md](ENVIRONMENT_VARIABLES.md) — Pipeline / CPU budget
- [CHUNK_AND_CDC.md](CHUNK_AND_CDC.md) — CDC 与 chunk 边界
