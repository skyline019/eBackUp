# eb-sync — 外侧云同步 CLI

独立于 `engine_cpp` 内核；只读调用 `ebbackup_core`（delta 导出、superblock 读取）。

## 构建

```powershell
cmake -S e:\recoveryProjects -B e:\recoveryProjects\build -DEBBACKUP_BUILD_TESTS=ON -DEBBACKUP_BUILD_SYNC=ON
cmake --build e:\recoveryProjects\build --config Release --target eb-sync
```

## 无云起步（推荐）

### 摆渡模式

```powershell
eb-sync init --repo ./repo --mode ferry
eb-sync ferry export --repo ./repo --out-dir D:\ferry --auto-base
eb-sync ferry import --base ./base.ebb --delta D:\ferry\delta_1_2.ebb --dest-repo ./imported
```

### 本地镜像

```powershell
eb-sync init --repo ./repo --local-mirror D:\sync-mirror
eb-sync push --repo ./repo --once
eb-sync pull --repo ./repo --dest ./restored
```

环境变量覆盖（开发）：`EBSYNC_LOCAL_ROOT=D:\sync-mirror`

## S3 配置（可选）

仓库旁 `sync.json`（密钥勿提交 git）：

```json
{
  "remote_type": "s3",
  "endpoint": "http://127.0.0.1:9000",
  "region": "us-east-1",
  "bucket": "ebbackup",
  "prefix": "my-repo/",
  "access_key": "minioadmin",
  "secret_key": "minioadmin",
  "path_style": true
}
```

环境变量：`EBSYNC_S3_ENDPOINT`、`EBSYNC_S3_BUCKET`、`EBSYNC_S3_ACCESS_KEY`、`EBSYNC_S3_SECRET_KEY` 等。

## 阿里云 PDS（网盘）

RAM OAuth 应用凭证 CSV（`appId,appSecretId,appSecretValue`）**勿提交 git**。

```powershell
eb-sync init --repo ./repo --pds --domain bj36449 --credentials C:\path\appSecret.csv
eb-sync pds auth-url --repo ./repo
# 浏览器授权后
eb-sync pds auth --repo ./repo --code <授权码>
eb-sync push --repo ./repo --once
```

对象布局与 S3 相同，映射到网盘目录 `{root_prefix}/chunks/...`、`meta/...`、`remote_index.json`。  
默认 `root_prefix=ebbackup/{仓库目录名}`。示例配置见 `engine_cpp/config/sync_pds.example.json`。

环境变量：`EBSYNC_PDS_DOMAIN`、`EBSYNC_PDS_CLIENT_ID`、`EBSYNC_PDS_CLIENT_SECRET`、`EBSYNC_PDS_REFRESH_TOKEN`、`EBSYNC_PDS_DRIVE_ID`。

## 命令

```bash
eb-sync init --repo ./repo --local-mirror D:\mirror
eb-sync init --repo ./repo --mode ferry
eb-sync init --repo ./repo --endpoint http://127.0.0.1:9000 --bucket ebbackup --prefix repo1/ --path-style --access-key ... --secret-key ...
eb-sync init --repo ./repo --pds --domain DOMAIN --credentials appSecret.csv
eb-sync pds auth-url --repo ./repo
eb-sync pds auth --repo ./repo --code CODE
eb-sync status --repo ./repo --json
eb-sync push --repo ./repo --once
eb-sync ferry export --repo ./repo --out-dir D:\ferry --auto-base [--also-mirror]
eb-sync ferry import --base ./base.ebb --delta ./delta.ebb --dest-repo ./out
eb-sync maintenance-check --repo ./repo
```

状态文件：`{repo}/catalog/sync_state.json`、`sync_outbox.jsonl`

详见 [docs/technical/CLOUD_ECOSYSTEM.md](../docs/technical/CLOUD_ECOSYSTEM.md)。
