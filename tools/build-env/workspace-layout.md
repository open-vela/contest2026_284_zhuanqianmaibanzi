# 工作区布局与整理记录

本页包含 2026-10-24 只归档和 2026-10-31 压缩清理两次记录。最新清理不修改
业务代码、配置或当前固件，不清空增量构建产物；当时未提交/推送 Git 改动。
2026-11-01 按用户要求另行整理上库，见
[改动清单](../../board/contest_board/docs/change-summary-2026-11-01.md)。

`board/contest_board/common/board` 是 NuttX `tools/Unix.mk` 自动生成的板级源码
链接，目标包含本机工作区路径。现在取消 Git 跟踪并加入忽略规则，本地链接保留，
新工作区由构建系统生成；这不删除或移动 `board/contest_board/src/` 源码。

## 最新清理（2026-10-31）

- 两份不可用旧验证树原占约 710 MiB，先完整归档为根工作区的
  `recovery-backup/archives/legacy-checks-2026-10-31.tar.zst`，再执行压缩完整性
  检查和 `tar --compare` 逐文件比对；均通过后移除两份展开目录。
  归档保留文件内容、权限、硬链接和符号链接，不跟随旧 apps/prebuilts 链接。
- 归档约 162 MiB，SHA-256：
  `75ad9b8be0cc148c0d43a1d10a749b8ad17c3c5425018f38fdf2c315c27ae397`。
- 清理 `tests/host/__pycache__`，增加字节码忽略和 IDE 隐藏规则；测试源文件不删。
- 新增 `out/README.md` 索引和 `out/current` 快捷链接，指向最新 resilient 发布。
  保留两种时钟新固件、用户反馈可上传的旧候选及 NSH 恢复固件；旧交付链接不破坏。
- 保留所有公共构建树、工具链、repo 元数据、AI/比赛记录、原理图和提交材料。
  `upstream/nuttx` 有未提交的 esp_timerisr.c 修改，`local/` 有旧 HAL 修改，均不删。
- 清理前后核对业务/BSP配置、历史定时器修改、当前 .config、固件和最新交付包
  校验值；不重新构建、不改变当前默认 1 MHz 配置或实板结论。

需要恢复旧验证树时，在根工作区执行（不会覆盖已有文件）：

```sh
tar --zstd --keep-old-files -xf recovery-backup/archives/legacy-checks-2026-10-31.tar.zst \
  -C recovery-backup/legacy-checks/
```

恢复不修复其指向已删除旧 contest284 目录的 Git/依赖链接，不能把旧副本当作
当前构建树。清理结果见 `board/contest_board/validation/2026-10-31-workspace-cleanup.txt`。

## 日常维护

工作区根目录是 `/home/benson/openvela-work`。其下各公共目录由 repo 管理；
作品独立 Git 仓为 `contest284-manifest`，不要在根目录运行 `git add .`。

| 目录/入口 | 用途 | 本次处理 |
| --- | --- | --- |
| `contest284-manifest/app/` | 作品应用维护源 | 保留路径与全部改动 |
| `contest284-manifest/board/contest_board/` | BSP 维护源 | 保留路径与全部改动 |
| `contest284-manifest/tools/` | Windows 接收端、环境恢复材料 | 保留；补充本说明 |
| `contest284-manifest/out/` | NSH/产品固件归档和 Windows 发布包 | 保留；不是源码 |
| `nuttx/`、`apps/`、`build/`、`external/`、`frameworks/`、`packages/`、`vendor/`、`tests/`、`docs/` | 公共构建树 | 保留，不移动 |
| `.repo/` | repo 元数据与公共 Git 对象 | 保留，不清理 |
| `openvela-prebuilts-kept/`、`prebuilts` | 原工具链及其映射 | 保留，不移动 |
| `local/`、`upstream/` | HAL 缓存、参考源码及原理图 | 保留，不移动 |
| `logs/` | 历史构建、串口日志与锁定清单 | 保留历史路径；本次恢复日志另行分类 |
| `recovery-backup/` | 恢复前配置备份和旧验证树压缩归档 | 旧展开目录已在校验压缩包后移除，可恢复 |
| `WORKSPACE.md` | 根目录导航 | 新增 |
| `contest284-manifest/VelaPoka.code-workspace` | 可选 VS Code 工作区与构建任务 | 新增，不改现有 IDE 设置 |

`build.sh`、`prebuilts`、团队目录别名、vendor 板级目录和 apps 示例应用的符号
链接都是运行映射，并非待删除的重复源码。

## 本次归档（2026-10-24）

两份旧目录分别占用约 344 MiB、366 MiB，其 `.git`、apps 和工具链链接指向已
删除的旧 `contest284/` 工作区，不能当作当前可构建工作树。只移动，不修复或
删除历史内容：

```text
.vendor-baseline-check-1786288232
  → recovery-backup/legacy-checks/.vendor-baseline-check-1786288232
.vendor-delivery-check-1786289082
  → recovery-backup/legacy-checks/.vendor-delivery-check-1786289082
```

13 个恢复文件从 `logs/` 移入 `logs/build-env/2026-10-24/`，文件名不变：

- `workspace-restore-2026-10-24-{clean,hal-submodule,nsh-attempts,nsh,refresh,sync}.log`；
- `workspace-restore-2026-10-24-velapoka{,-local-cache-attempt,-raw10-attempt,-timer-attempt}.log`；
- `workspace-restore-pr{371,372,373}.patch`。

构建归档中的日志副本不移动。`logs/contest284_synced_manifest.xml` 和早期
串口/开发日志保留原路径，避免影响恢复依据及历史证据引用。
以上移动可按反向路径恢复；没有删除数据，也不宣称释放磁盘空间。

## IDE 入口

在 VS Code 中选择“从文件打开工作区”，打开维护仓根部的
`VelaPoka.code-workspace`，或在工作区根目录运行：

```bash
code contest284-manifest/VelaPoka.code-workspace
```

可分别浏览 `VelaPoka`、`NuttX`、`NuttX Apps`，默认终端仍位于构建工作区根目录。
默认构建任务为产品配置，另有 NSH 恢复配置；两者都不会烧录。
**不要同时运行两套配置的构建任务**，它们共享 NuttX 输出目录。
切换配置前先保存需要的镜像；后续构建仍可能回写 defconfig。

该配置只隐藏中间目标文件和自动拉取的 HAL/ESP-Hosted 目录，并跳过归档日志、
输出目录的搜索/监听。它不删除文件；需要研读 HAL 时可取消对应 `files.exclude`
和 `search.exclude` 项。原来直接打开整个根目录的 IDE 会话不受影响。

## 固件边界

当前构建输出为 `nuttx/vela_nuttx.bin`，用户重新构建后内容可以变化。
恢复阶段归档的两套固件仍在 `contest284-manifest/out/firmware/2026-10-24/`，
不能把归档固件的旧 SHA 自动套用到最新构建输出。本次整理没有重建或替换固件。

整理前后应核对业务/BSP 差异、上传源码、当前 `.config` 和固件哈希，以及运行
映射是否有效。整理不增加任何实板验收结论。
