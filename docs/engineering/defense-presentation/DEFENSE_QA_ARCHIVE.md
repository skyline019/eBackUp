# ebbackup 答辩技术细节 Q&A 模拟归档

**版本**：v0.10.3 · **ABI v37** · **gtest 393** · **Workbench IT 13**  
**用途**：答辩会「所有详细技术细节」备答；每题含**考察点**、**标准回答**、**追问深挖**、**索引**。  
**互补材料**：[`slides.pdf`](build/slides.pdf) · [`script.pdf`](build/script.pdf) · [`kernel-manual/`](../kernel-manual/) · [`windows-native/`](../windows-native/)

---

## 如何使用本文

| 场景 | 建议 |
|------|------|
| 评委 30 秒快问 | 只背每题 **标准回答** 第一段 |
| 评委 2 分钟深挖 | 加 **追问深挖** + 一个代码/数据点 |
| 演示前彩排 | 按 Part A→M 顺序过一遍；Windows/VSS 专册对应 F 章 |
| 答辩后归档 | 本文 + `script.pdf` 附录一并提交 |

**口径原则**：先答「做了什么、为什么、怎么验证」，再答实现细节；第三方库（LZ4/zstd/crypto）主动说明课程扩展分规则，强调**自研内核**（CDC、耐久性、Pipeline、bench 门禁、Win 元数据）。

---

## Part A — 课程与项目定位

### A1. 本项目对应课程哪几条要求？和评分表怎么对齐？

**考察点**：是否理解「综合实验」交付物，而非只讲技术炫技。

**标准回答**：  
本项目对应《软件开发综合实验》的完整软件生命周期：需求（备份/恢复/增量/Windows 保真）、设计（八层架构 + 构件图）、实现（C++ 内核 + CLI + Workbench）、测试（393 gtest + bench 门禁 + 13 项 GUI 集成测试）、发布（版本 v0.10.3、ABI 文档、CHANGELOG）。  
用例与功能对照见 `defense-presentation/chapters/course-mapping.tex`；类/顺序/构件图在 `kernel-manual` 与答辩 tex 图；测试报告对应 `TEST_AND_CI.md` 与 `ci_floor.json`。

**追问深挖**：  
- *「UML 四图在哪里？」* → StarUML 可作附录；仓库内 `figures-architecture.tex`、`figures-sequence.tex`、`figures-components.tex` 是答辩口径的正式 tex 表述。  
- *「第三方库算扩展分吗？」* → LZ4/zstd/OpenSSL 类库按课程规则扩展分可能 50%；答辩重点在 FastCDC、提交点 fsync、Manifest v5、VSS 闭包等自研逻辑。

**索引**：`docs/engineering/defense-presentation/chapters/course-mapping.tex`

---

### A2. 为何选备份而不是网盘/同步盘？

**考察点**：产品边界（P5 单人维护带宽）。

**标准回答**：  
ebbackup 定位是**单节点可验证的内容寻址备份内核**，不是多用户云存储。网络协议、账户体系、多端冲突合并会分散维护带宽，与「Windows Release + bench 立法」的本土化策略冲突。外侧云能力由 `sync_cpp`（S3/PDS/摆渡）在**内核冻结边界外**扩展，见 `CLOUD_ECOSYSTEM.md`。

**索引**：`docs/technical/CLOUD_ECOSYSTEM.md`

---

### A3. 和 restic、borg、Veeam 比，创新点在哪？

**考察点**：竞品认知 + 诚实边界。

**标准回答**：  
与 restic/borg 同属主机级 content-addressed backup；我们差异在：  
1) **Windows Release bench 门禁**（L1–L7，immutable floor）；  
2) **EbPack** 默认 pack 布局 + 持久化索引；  
3) **Manifest v5** 完整 Win 元数据（ACL/ADS/硬链/junction/sparse/EFS）；  
4) **VSS** crash/app/auto + 多卷 junction 闭包（文件级，非块镜像）；  
5) **RAR/Merkle** 审计链与可证明 Diff。  
**不做** Veeam 级 VM 块镜像、Hyper-V 集成、Exchange/SQL 专用 Writer——战略边界见答辩 script 与 `BACKUP_CAPABILITY_GAPS.md`。

**索引**：`docs/product/BACKUP_CAPABILITY_GAPS.md` · `WAVE_ARCHIVE.md`

---

## Part B — 架构与备份 FSM

### B1. 备份引擎分几层？各层职责？

**考察点**：分层是否清晰。

**标准回答**：  
自上而下：CLI/Daemon/C API → **BackupEngine**（FSM、事务、manifest、恢复）→ **BackupPipeline**（调度、分块、压缩、存储队列）→ Chunk/HCRBO、Compress、ChunkStore、Snapshot/GC → Crypto/Digest/Durability/Superblock。  
GUI 通过 `ebbackup_workbench.dll` JSON shim 调用 C API，**不复制**业务逻辑。

**索引**：`docs/technical/ARCHITECTURE.md` · `figures-architecture.tex`

---

### B2. 备份 FSM 有哪些状态？断电会怎样？

**考察点**：耐久性（恒定约束）。

**标准回答**：  
`Idle → Scanning → Chunking → Storing → CommittingMeta → Auditing → Complete`；异常 → `Aborted`。  
**提交点耐久性**：manifest 写入 superblock 前必须 `chunk_store_->Flush()`（fsync）。中途 crash 时 txn 标记 `kAborted`，**前一 snapshot 仍可 verify/restore**。Coalesced meta（v0.5+）减少 fsync 次数，但 commit 点仍强制落盘。

**追问深挖**：  
- *「双槽 superblock 干什么？」* → 4KB×2 slot + CRC 选举，防止写一半损坏。  
- *「和 WAL 数据库比？」* → 我们的是备份快照语义：chunk 先写、manifest 后 commit，类似 copy-on-write 提交点。

**索引**：`docs/technical/STORAGE_AND_DURABILITY.md` · `docs/technical/ARCHITECTURE.md`

---

### B3. Content ID 是什么？优化能改 hash 吗？

**考察点**：内容寻址不变量。

**标准回答**：  
Content ID = **`SHA256(明文 chunk payload)`**，全仓库恒定约束。任何 Pipeline 路径、CDC 实现、digest 栈（Legacy/Standard/SHA-NI）优化必须通过 **parity 测试**，不得改变 chunk 边界或 hash。这是增量 dedup 与跨版本 restore 的根基。

**索引**：`docs/technical/CHUNK_AND_CDC.md` · `docs/README.md` 恒定约束表

---

## Part C — CDC / 分块

> **深挖专章**：Pipeline 从零拓扑、L5/L7 跃升数据、CDC cut/gear 漂移与 parity 立法 → [`PIPELINE_CDC_DEEP_DIVE.md`](PIPELINE_CDC_DEEP_DIVE.md)

### C1. 为什么用 FastCDC 而不是固定 4MB 分块？

**考察点**：CDC 动机。

**标准回答**：  
固定分块在插入/删除字节后边界全漂，dedup 率差。FastCDC 用 Gear hash rolling window，按 min/avg/max chunk size 自适应切点，对增量友好。实现于 `fast_cdc.cc` / `fast_cdc_simd.cc`（AVX2）。

**索引**：`docs/technical/CHUNK_AND_CDC.md` · **`PIPELINE_CDC_DEEP_DIVE.md`** §四

---

### C2b. 「CDC 漂移」具体指什么？怎么保证 stream 和 slice 切点一致？

**考察点**：Content ID 不变量 + 工程立法。

**标准回答**：  
**Cut 漂移** = 流式 `FastCdcStreamFeed` 与 golden `FastCdcSlice::Chunk` 切点 offset 不一致 → SHA256 变、dedup 失效。**Gear 漂移** = 跨 32MB feed 边界 rolling hash 状态未连续。  
保障：① carry tail + `StreamSegmentView`；② v0.9.4 Phase B carry 消耗完后 **handoff 到 contiguous seg1 loop**；③ CI：`fast_cdc_streaming_test`、`ChunkCutsUntilMatchesFullCuts`、256MB manifest parity；④ Phase 3C carry-split **parity 失败已回退**。

**索引**：`PIPELINE_CDC_DEEP_DIVE.md` §4 · `fast_cdc_streaming.cc`

---

### C2. 流式 CDC 和 mmap 整文件 CDC cut 点一致吗？

**考察点**：Sprint 4 parity 约束。

**标准回答**：  
**必须 bit-identical**。`FastCdcStreamFeed` 是生产路径（32MB feed + encode overlap）；`FastCdcSlice::Chunk` 是 golden reference。v0.9.4 Phase B 在 carry 消耗完后 handoff 到 unified seg1 loop，禁止错误 `boundary_limit` 导致 cut 偏移。

**追问深挖**：  
- *「Phase A 为何默认关？」* → 整文件 `ChunkCuts()` 后主线程才 push，L5 **105 vs 171 MB/s（-39%）**，overlap 丧失；L3b ratio 仍可能达标。

**索引**：`PIPELINE_CDC_DEEP_DIVE.md` §3.5 · `backup_pipeline.cc`

---

### C3. HCRBO / CFI 是什么？

**考察点**：增量扫描优化。

**标准回答**：  
**HCRBO**（Hierarchical Chunk Rolling Bloom Oracle）用于备份时快速判断文件内容是否变化，减少全量 CDC。**CFI**（Chunk Feature Index）配合 Bloom 过滤，跳过未变 chunk 的重复 hash。二者不改变 Content ID，只影响**是否读盘/是否 chunk**。

**索引**：`kernel-manual` CDC 章 · `WAVE_ARCHIVE.md` Wave 2

---

## Part D — Pipeline 与性能

> **深挖专章**：v0.4.6 四阶段 → v4 → Streaming 分化、五次跃升数据、Phase A 负向实验 → [`PIPELINE_CDC_DEEP_DIVE.md`](PIPELINE_CDC_DEEP_DIVE.md)

### D1. Pipeline v4 拓扑是什么？

**考察点**：并发模型。

**标准回答**：  
v0.8 **Pipeline v4**：全局 chunk/encoded 队列 + M 个 Store worker + FileAggregator，支持 L7 multi-file 并行。小文件（≤32MB）走 **inline sequential** 快路径（v0.9.0）；大文件（>32MB）走 **StreamingChunkCpuPipeline**（v0.9.2）。

**索引**：`docs/technical/ARCHITECTURE.md` · `PIPELINE_CDC_DEEP_DIVE.md` §二

---

### D1b. Pipeline 从零怎么设计的？为什么 v0.8 后 L5 仍卡在 ~146？

**考察点**：拓扑分化动机。

**标准回答**：  
**v0.4.6** 首版 4-stage 单线程：mmap Read → FastCdcSlice Chunk → Encode → Store，无 overlap。**v0.6** +2 Store worker + EbPack → L5 ~139。**v0.8 v4** 全局 chunk/encoded 队列服务 L7（~554+ MB/s），但单大文件 profile 显示 chunk **占 98%**——瓶颈在 CDC+digest 串行，堆 worker 无效。  
**v0.9.2** 决策：**路由分化**——≤32MB inline；>32MB `StreamingChunkCpuPipeline`（32MB feed + 2 encode + 1 store overlap），**不做 Pipeline v5 全量替换**（P5 维护成本）。

**索引**：`PIPELINE_CDC_DEEP_DIVE.md` §一–§三 · `ecosystem-evolution` 第 7–8 章

---

### D1c. 性能跃升的关键数字？（答辩背表）

**考察点**：数据立法。

**标准回答**：

| 节点 | L5 256MB | 驱动因素 |
|------|----------|----------|
| v0.6 | ~139 | EbPack + 双 Store |
| v0.8 | ~146 | v4 修 L7 非 L5 |
| v0.9.0 | 142–149 | ChunkStore 并发修复 |
| v0.9.2 | ~154 | Streaming overlap |
| v0.9.3 | 159–163 | digest_base |
| v0.9.4 | **170–172** | Phase B seg1 bulk |

v0.9.x 四轮 **+18% L5**；Phase A 若默认则 **105 MB/s（-39%）**——immutable ratio 捕不到，故 L5 绝对门禁 105。

**索引**：`PERF_BASELINE.md` · `PIPELINE_CDC_DEEP_DIVE.md` §三

---

### D2. L1–L7 bench 分别测什么？当前多少 MB/s？

**考察点**：性能立法。

**标准回答**：  
L1 小文件、L3a/L3b 增量 ratio、L5 256MB 单文件、L7 multi-file 等；**immutable floor**：`reuse_pct_min≥90`、`pipeline_ratio*_min≥0.90` **只允许升高不允许降低**。  
v0.9.6 参考：L5 ~170–172 MB/s，L7 ~646–658 MB/s，L3b ratio ~1.01–1.05（见 `docs/README.md` 版本快照表）。

**追问深挖**：  
- *「负向实验 IV 是什么？」* → Phase A FastCdcSlice 曾导致绝对 MB/s 崩溃而 ratio 仍达标；immutable ratio **捕不到**此类劣化，故增加默认路径 + 绝对 MB/s 产品守卫。

**索引**：`docs/reference/PERF_BASELINE.md` · `defense-presentation/main-script.tex` 实验 IV 节

---

### D3. CompressTier 和仓库字典干什么？

**考察点**：ABI v30 压缩。

**标准回答**：  
**CompressTier** 按内容类别（text/binary/encrypted 等）选 LZ4/zstd 档位；**Zstd LDM** + **仓库级字典**提升重复结构压缩率。`eb repo-stats` 报告 `live_uncompressed_bytes`、`compress_ratio`、`has_zstd_dict` 等（ABI v30）。压缩在 chunk payload 层，与 Content ID（hash 明文）分离：存的是压缩后 bytes，restore 解压后 hash 仍对。

**索引**：`docs/technical/COMPRESSION.md` · ABI v30 表

---

## Part E — 存储 / Manifest / EbPack

### E1. EbPack 和 tar 打包有何不同？

**考察点**：课程「打包解包」口径。

**标准回答**：  
EbPack 将 chunk **内容寻址**聚合为 `.ebpack` 文件，带索引与 shard，支持 dedup 引用与 GC；不是 tar 式顺序归档。答辩强调 **dedup + 索引 + 不可变 chunk**，而非「单文件打包作业」。

**索引**：`docs/technical/STORAGE_AND_DURABILITY.md`

---

### E2. Manifest v4 和 v5 区别？何时升级 v5？

**考察点**：Windows 持久化。

**标准回答**：  
v4 inner magic `0xEB4F0001`，v5 为 `0xEB4F0002`，header `EBMANIFEST5`。v5 在 v4 Unix meta flags 上扩展：`kMetaWinSd(0x20)`、`kMetaInode(0x40)`、`kMetaReparse(0x80)`、`kMetaStream(0x100)`、`kMetaReparseTarget(0x200)`、`kMetaSparse(0x400)`、`kMetaEfs(0x800)`。  
`WriteManifestAuto`：任一 entry `has_win_meta()` / `has_sparse_meta()` / `has_efs_meta()` → 写 v5；纯 Linux 仓库仍 v4。

**追问深挖**：  
- *「单条 record 布局？」* → path + size + chunk_hashes + 按 meta_flags 顺序的可选块 + body CRC32。  
- *「空 ACL 写不写 flag？」* → 不写 `kMetaWinSd`，`BuildV5MetaFlags` 按需 OR。

**索引**：`docs/engineering/windows-native/chapters/01-manifest-v5.tex` · `manifest.cc`

---

### E3. 快照 GFS 保留策略如何实现？

**考察点**：运维闭环。

**标准回答**：  
`eb backup` 产生 txn/snapshot；`eb prune` 按 GFS 策略删除不可达 snapshot；**Wave A** 起 `AnalyzeSnapshotReachability` + `eb verify-chain` 保证增量链可达（GAP-CONSIST-04）。WORM/immutable 用 `kBackupFeatureImmutable` + `immutable_until` 门控 prune。

**索引**：`docs/product/BACKUP_CAPABILITY_GAPS.md` GAP-JOB-04

---

## Part F — Windows 原生元数据（答辩高频）

> 详述见 [`windows-native/`](../windows-native/) 9 册 PDF；以下为答辩浓缩答法。

### F1. 为什么 Unix mode 不够？ACL 怎么备份？

**考察点**：GAP-WIN-01。

**标准回答**：  
NTFS 权限由 **SECURITY_DESCRIPTOR**（Owner/Group/DACL）承载，不是 Unix mode。我们**不解析 ACE**，整段 SD `GetFileSecurityW` → 自研 Base64 → manifest `kMetaWinSd` → 恢复 `SetFileSecurityW` 字节级 roundtrip。  
恢复四策略：**inherit**（默认）、**preserve**、**skip**、**best_effort**（失败记 `acl_apply_failed` issue 不 abort）。

**索引**：`win_meta.cc` · `windows-native/03-acl-sd.pdf`

---

### F2. ADS 怎么扫描和恢复？

**考察点**：GAP-WIN-02。

**标准回答**：  
`FindFirstStreamW` 枚举命名 stream；跳过默认 `::$DATA`；每条 ADS 追加独立 `ScanEntry`，`relative_path` 形如 `file.txt:Zone.Identifier`，manifest 设 `kMetaStream`。恢复时路径含 `:`，`CreateFileW` 打开 alternate stream 写入。Hardlink dedup 键 `{inode_id, stream_name}` 防止 ADS 与主 $DATA 错误合并。

**索引**：`scan_entry.cc` AppendAlternateStreams · `windows-native/04-ads.pdf`

---

### F3. 硬链接如何 dedup？恢复怎么建链？

**考察点**：GAP-WIN-04。

**标准回答**：  
采集：`GetFileInformationByHandle` → `nFileIndexHigh/Low` 合成 `inode_id`。  
备份：BackupEngine 维护 `(inode_id, stream_name) → canonical manifest index`，第二条路径**复用 chunk 列表不读盘**。  
恢复：`RestoreInodeKey` 表，首路径写内容，后续 `CreateHardLinkW`。

**索引**：`backup_engine.cc` · `restore_engine.cc` · `windows-native/05-hardlink-inode.pdf`

---

### F4. Junction 为什么不递归扫描？恢复怎么办？

**考察点**：GAP-WIN-03 双轨策略。

**标准回答**：  
**扫描层**：reparse 目录记 `reparse_junction` issue，**不 push** walk 栈，避免扫进整盘 D:。仍 `CaptureWinMetaFromPath` 采集 `reparse_tag` + `reparse_target`（`FSCTL_GET_REPARSE_POINT`）。  
**VSS 层**：`ProbeJunctionVolumes` BFS（默认深度 2）把 target 卷纳入 snapshot set——**扫描不展开 ≠ 快照不含该卷**。  
**恢复**：`ReparseRestorePolicy` skip 或 recreate（`mklink /J` 优先，fallback `FSCTL_SET_REPARSE_POINT`）。

**索引**：`windows-native/06-reparse-junction.pdf` · `vss_volume_closure.cc`

---

### F5. 稀疏文件如何避免备份膨胀？

**考察点**：GAP-WIN-05 / ABI v33。

**标准回答**：  
检测 `FILE_ATTRIBUTE_SPARSE_FILE`；`FSCTL_GET_RETRIEVAL_POINTERS` 得 `sparse_runs`（LCN=-1 为 hole 跳过）；只对 allocated run 读盘 chunk，manifest 存 `sparse_chunk_offsets` 供恢复 seek。恢复：`FSCTL_SET_SPARSE` + `SetFilePointerEx` 按 offset 写。CLI `--sparse auto|off`，flag `EB_BACKUP_FLAG_SPARSE_OFF`。

**索引**：`sparse_file.cc` · `windows-native/07-sparse-ntfs.pdf`

---

### F6. EFS 分 Tier A/B 原因？密钥存哪？

**考察点**：GAP-WIN-06 / ABI v37。

**标准回答**：  
EFS 是 NTFS 文件系统级加密，与仓库 AES-GCM **正交**。  
**Tier A（默认）**：检测 `FILE_ATTRIBUTE_ENCRYPTED`，`needs_chunking()==false`，不读明文，issue `efs_encrypted_skipped`。  
**Tier B**：`--efs-export-keys` → `ReadEncryptedFileRaw` → manifest `kMetaEfs`（`efs_key_blob_b64`）→ 恢复 `WriteEncryptedFileRaw`。默认 Tier A 是为避免无意把密钥 material 写入仓库。

**索引**：`efs_key.cc` · `windows-native/08-efs.pdf`

---

### F7. EnrichScanEntriesWinMeta 在流水线什么位置？

**考察点**：扫描架构。

**标准回答**：  
DFS `ScanDirectory` 完成后**后处理**：对 `base_count` 条 entry 调用 `CaptureWinMetaFromPath`（ACL/inode/reparse），再 sparse/EFS/ADS。单路径失败记 `scan_issues.unreadable`，**不 abort** 整库。VSS 启用时在 **shadow 映射路径**上 enrich，保证快照时间点一致。

**索引**：`windows-native/02-scan-enrichment.pdf`

---

## Part G — VSS 与一致性

### G1. VSS crash / app / auto 区别？

**考察点**：GAP-CONSIST-01 / ABI v31–32。

**标准回答**：  
- **crash**：`DoSnapshotSet` 后直接读 shadow，无 Writer 协调；  
- **app**：`GatherWriterMetadata` → 快照 → 读 → `GatherWriterStatus` → **读完才** `BackupComplete`；  
- **auto**：尝试 app，Writer 失败降级 crash，issue `vss_writer_degraded`。  
定位：**文件级**一致性读，**不做**块级 raw 镜像。

**索引**：`windows-native/09-vss.pdf` · `docs/technical/VSS.md`

---

### G2. 影子存储预检怎么做？不足怎么办？

**考察点**：Phase 18 / ABI v35。

**标准回答**：  
`CheckShadowStoragePreflightEx`：WMI `Win32_ShadowStorage` 优先，`vssadmin list shadowstorage` 文本 fallback；默认要求 **512MB** 余量。不足时 fail，或 `--vss-fallback-live` 降级活扫描并 issue `vss_shadow_storage_low`。CLI `eb vss status` 诊断。

**索引**：`vss_shadow_storage.cc`

---

### G3. 锁定文件怎么处理？

**考察点**：GAP-CONSIST-02。

**标准回答**：  
活扫描：`ERROR_SHARING_VIOLATION` → issue `locked`，pipeline skip。启用 VSS 后 shadow 路径常可读。报告 JSON 显式列出 `scan_issues.locked` / `unreadable`，**尽力一致 + 未备份清单**，非静默跳过。

**索引**：`scan_entry.cc` · `BACKUP_CAPABILITY_GAPS.md`

---

## Part H — 恢复 / 冲突 / 验收

### H1. 三路就地恢复是什么？

**考察点**：GAP-RESTORE-01 / ABI v23。

**标准回答**：  
`preview/apply_in_place_json` 支持 **base（备份）/ target（选定快照）/ live（当前磁盘）** 三路合并；冲突标记 `both_changed`；`--base-at` 指定基线 txn。Dry-run + GUI 冲突筛选与 bulk 决议（GAP-RESTORE-04）。

**索引**：ABI v23 表 · `WORKBENCH_GUI.md`

---

### H2. 恢复验收报告 export 什么？

**考察点**：GAP-RESTORE-03。

**标准回答**：  
`eb_backup_export_restore_report_json`：写入字节数、跳过文件、ACL soft issue、EFS/sparse 专项 issue 等；Workbench OutputPanel 验收页签展示。

**索引**：`restore_engine.cc` · ABI v15+

---

## Part I — 加密 / 密钥 / 审计

### I1. 仓库加密和 EFS 有什么区别？

**考察点**：两层加密。

**标准回答**：  
仓库 **AES-GCM** 保护 chunk payload（`EB_BACKUP_FLAG_ENCRYPT`）；**EFS** 保护 NTFS 上源文件。可同时存在：备份 EFS 文件时 Tier A 可能无 chunk，Tier B 存密钥 blob，仓库仍整体加密。

**索引**：`windows-native/08-efs.pdf` · `CRYPTO` 相关 kernel-manual 章

---

### I2. 恢复密钥 envelope 解决什么问题？

**考察点**：GAP-KEYS-01 / ABI v34。

**标准回答**：  
`crypto.envelope.json`：主密钥双包裹（密码 + **recovery_key**），防单点遗忘密码导致仓库永久不可读。API：`unwrap_with_recovery_key`、`rotate_password`；Workbench 恢复密钥向导（Phase 17–19）。

**索引**：`docs/technical/ABI_AND_FEATURES.md` v34 节

---

### I3. RAR 审计链是什么？

**考察点**：可证明性。

**标准回答**：  
破坏性操作（prune、GC、immutable 变更）append-only 写入 `audit/rar.chain`；GUI ops 亦审计（GAP-KEYS-02 ABI v25）。与 Merkle manifest 子集校验配合，支持 diff 证明（GAP-CATALOG-03）。

**索引**：`WAVE_ARCHIVE.md` Wave R 前后

---

## Part J — GUI / C API / 作业

### J1. 为什么 Tauri+Vue 而不是 Qt？

**考察点**：课程 UI 规则。

**标准回答**：  
课程允许 UI 用脚本技术栈；选 Tauri+Vue 复用 eB-Tree Workbench 壳层经验，**内核仍为 C++**，符合「后台 C++、前台可脚本」。业务逻辑只在 `engine_cpp`，DLL 为 JSON shim，不复制引擎。

**索引**：`defense-presentation/main-script.tex` Q&A · `gui/README.md`

---

### J2. jobs.json 解决什么？

**考察点**：GAP-JOB-01。

**标准回答**：  
多源多作业配置持久化；`RunJob` 共享 chunk store；字段含 exclude、window、VSS、plugins、webhook 等。队列：`job_queue.jsonl` + `eb queue`（GAP-ENGINE-02）。

**索引**：ABI v18–22 · `CAPABILITY_ROADMAP.md`

---

### J3. 垂直插件有哪些？

**考察点**：Wave P / GAP-VERTICAL。

**标准回答**：  
`IBackupPlugin` 接入 `RunBackup`：内置 **sqlite_checkpoint**、**registry_hive**、**vhdx_scan**。不含 SQL/Exchange **专用 VSS Writer**——依赖系统已注册 Writer。

**索引**：ABI v27 · `BACKUP_CAPABILITY_GAPS.md` §I

---

## Part K — 测试 / CI

### K1. 测试分几层？多少用例？

**考察点**：质量门禁。

**标准回答**：  
- **gtest 393**（`ebbackup_tests`）：单元 + 集成 + winmeta E2E + manifest roundtrip + VSS stub；  
- **C API** 子集测试；  
- **ebsync_tests 6**；  
- **Workbench Rust IT 13**；  
- **bench ctest** L1–L7 + immutable floor。  
Windows 专项：`acl_restore_test`、`ads_backup_restore_test`、`hardlink_backup_restore_test`、`sparse_backup_restore_test`、`efs_backup_restore_test`、`vss_*_test`。

**索引**：`docs/technical/TEST_AND_CI.md`

---

### K2. manifest v5 怎么测 roundtrip？

**考察点**：二进制兼容性。

**标准回答**：  
`manifest_v5_test.cc` 覆盖全 flag 组合 Write/Read 对称；`manifest_v4_test.cc` 保证旧仓库可读；`DocumentUsesV5` 与 `WriteManifestAuto` 选型有独立断言。

**索引**：`engine_cpp/tests/engine/manifest_v5_test.cc`

---

## Part L — 边界 / 不做 / 许可证

### L1. 明确不做什么？

**考察点**：战略诚实。

**标准回答**：  
1) 块级/裸金属镜像；2) 内置网络备份与多用户；3) Veeam 级 VM/Hyper-V；4) SQL/Exchange 专用 Writer 插件；5) 影子空间管理 GUI。文件级 + VSS + Win 元数据是我们的主战场。

**索引**：`windows-native/09-vss.pdf` · `BACKUP_CAPABILITY_GAPS.md`

---

### L2. GPL v3 对商用嵌入的影响？

**考察点**：合规（若被问到）。

**标准回答**：  
`ebbackup_core` 为 GPL v3；**静态链**闭源商用受限。可行路径：**子进程**调用 CLI、**动态链** + 合规分发、或服务化。答辩如实说明许可证，不夸大商用承诺。

**索引**：根目录 `LICENSE` · 此前战略讨论口径

---

## Part M — 陷阱题与负向实验（加分）

### M1. 「Pipeline ratio 达标但 MB/s 崩了」说明什么？

**标准回答**：  
说明**单一指标门禁不够**；immutable ratio floor 捕不到绝对吞吐劣化。应对：默认路径守卫 + Phase A opt-in + 负向实验写入生态实录。体现「测量立法优于架构辩论」。

**索引**：`ecosystem-evolution/` · `main-script.tex` 实验 III/IV

---

### M2. 「Scan 不展开 junction 但 VSS 要包含 D: 盘」矛盾吗？

**标准回答**：  
不矛盾：**用户 manifest scope** 由 scan 决定（只含指定源树 + junction 元数据）；**一致性读能力**由 VSS 闭包决定（junction target 卷进 snapshot set）。故意解耦，避免 scope 爆炸同时保证跨卷读一致。

**索引**：`windows-native/06-reparse-junction.pdf` · `09-vss.pdf`

---

### M3. 「EFS Tier A 备份后 restore 是空文件」算失败吗？

**标准回答**：  
不算 silent failure：manifest 标记 `efs_encrypted`，报告 `efs_skipped_count`，restore issue `efs_encrypted_skipped_restore`。用户需显式启用 Tier B 并理解密钥合规。这是**默认安全**设计，答辩时说清 product trade-off。

**索引**：`efs_key.cc` · GAP-WIN-06

---

## 附录：答辩模拟脚本（30 分钟技术面）

| 分钟 | 评委角色 | 问题编号 |
|------|----------|----------|
| 0–3 | 课程评委 | A1, A3 |
| 3–8 | 系统评委 | B1, B2, B3, E2 |
| 8–14 | Windows 评委 | F1–F7（任选 4 题深挖） |
| 14–18 | 存储评委 | **D1b, D1c, C2b, C2**, D1, E1 |
| 18–22 | 一致性评委 | G1, G2, G3 |
| 22–26 | 恢复/安全 | H1, I1, I2 |
| 26–30 | 压轴 | M1, M2, L1 |

---

## 附录：文档与源码速查

| 主题 | 文档 | 源码 |
|------|------|------|
| **Pipeline/CDC 深挖** | **`PIPELINE_CDC_DEEP_DIVE.md`** | `backup_pipeline.cc`, `fast_cdc_streaming.cc` |
| Manifest v5 | `windows-native/01-*` | `manifest.cc` |
| Scan enrich | `windows-native/02-*` | `scan_entry.cc` |
| ACL | `windows-native/03-*` | `win_meta.cc` |
| ADS | `windows-native/04-*` | `scan_entry.cc` |
| Hardlink | `windows-native/05-*` | `backup/restore_engine.cc` |
| Junction | `windows-native/06-*` | `win_meta.cc` |
| Sparse | `windows-native/07-*` | `sparse_file.cc` |
| EFS | `windows-native/08-*` | `efs_key.cc` |
| VSS | `windows-native/09-*` | `vss_session.cc` |
| ABI | `ABI_AND_FEATURES.md` | `eb_backup.h` |
| 缺口表 | `BACKUP_CAPABILITY_GAPS.md` | — |
| Wave 归档 | `WAVE_ARCHIVE.md` | — |

---

*维护：与 VERSION_HISTORY / ABI / windows-native 专册同步更新。*
