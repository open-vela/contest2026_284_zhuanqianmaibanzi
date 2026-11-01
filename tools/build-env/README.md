# 完整构建环境恢复记录（2026-10-24）

工作区：`/home/benson/openvela-work`，维护仓：`contest284-manifest`。
本次复用原来保留的交叉工具链，恢复 `.repo`、公共源码、构建入口和运行映射；
不覆盖维护仓已有的独立展示/主动上传修改，不涉及烧录。

## 验证结果

两套配置构建均成功，esptool 镜像校验有效；产品 ELF 已确认包含
`velapoka_main`、`velapoka_upload_start` 和 `velapoka_storage_get_usage`。
全部公共恢复补丁已在隔离临时 Git 索引中从固定基线重放，与实际 NuttX
工作树逐项比较一致，不修改真实索引或用户工作树。

| 配置 | 镜像大小 | 维护仓中的产物目录 |
| --- | --- | --- |
| NSH | 252956 B | `out/firmware/2026-10-24/nsh/` |
| VelaPoka | 848880 B | `out/firmware/2026-10-24/velapoka/` |

每套保留镜像、ELF、map、完整 `.config`、生成的 defconfig 和构建日志。
当前根工作区 `nuttx/vela_nuttx.bin` 是产品镜像。哈希、命令和验收边界见
[构建验证记录](../../board/contest_board/validation/2026-10-24-build-env.txt)。
未连接开发板，未烧录；不得据此覆盖既有实板记录。

## 固定基线

- `manifest.lock.xml`：从清理前的 `logs/contest284_synced_manifest.xml` 保存，
  用于恢复原比赛公共依赖，避免直接跟随远端分支漂移。
- NuttX：`dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`。
- apps：`dcc6a95c3b323e533c98fde8fb209f99e24f0fdd`。
- ESP HAL：`8d0a898910084206721a0892ab093021bca1496a`，
  由 BSP 构建规则固定并应用 `chips/esp32p4/patches/` 的兼容补丁。
- 工具链：`prebuilts -> openvela-prebuilts-kept`；使用其
  `gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-gcc`。
- 镜像工具：esptool 4.8.1；配置工具：kconfig-conf、kconfiglib 14.1.0。

`patches/nuttx-pr371.patch` 是公共 PR #371 的完整两提交补丁，包含初始 P4
架构移植和 Kconfig 修复。仅应用旧的 `upstream/nuttx/0001-*` **不能**在原始
`dd92bcf` 上恢复完整支持。PR #372、#373 分别修复 USRSOCK 守卫和 SD 写完成校验。
三份原始补丁来自原公共 PR 的 `.patch` 下载，SHA-256 为：

```text
b478a719ab5c8ca2070777f7553b8aeda9916af3e77b16714677c617d9e9f819  nuttx-pr371.patch
d6c43186e96da8b877db21936af25e2a888dfeacb65ef8c5dc0f3e2fabee5167  nuttx-pr372.patch
5d1b5bd26cd07ebeba662b10e16dacd4073e0f7e80292b26f732cc2190bfb13b  nuttx-pr373.patch
```

注意：原 PR #373 将 `status` 错误声明在 `mmcsd_read()` 而不是
`mmcsd_write()` 中，直接使用会编译失败。此文件仅留作来源审计；实际恢复使用
维护仓已修正的 `board/contest_board/upstream/nuttx/0003-mmcsd-spi-verify-write-completion.patch`。

`patches/nuttx-esp32p4-spi-recovered.patch` 从归档会话
`logs/wx719/2026-08-30/codex__01a0528f-0d3f-72a2-b785-e0ed039a7bb3.jsonl`
第 1085 行的 `git diff` 输出恢复。它补齐当时未纳入公共 PR #371 的 SPI2
GPSPI HAL API、GPIO Matrix、RCC 时钟和空发送缓冲处理；不是本次重新设计驱动。
日志末尾缺少一行空白上下文，已修正最后一个 hunk 的行数。

`patches/nuttx-esp32p4-hr-timer-recovered.patch` 从归档会话
`logs/wx719/2026-08-23/codex__01a02f72-1853-79b2-ad0e-296adb2691ae.jsonl`
第 176 行恢复，补齐产品配置使用的 P4 高精度定时器 IRQ 命名兼容；
最小 NSH 不启用该功能，因此需要产品全量构建才能覆盖。

`patches/nuttx-raw10-capture-recovered.patch` 从 2026-08-20 会话
`codex__01a01fba-4cde-7900-aa6d-62b519a35825.jsonl` 第 519 行恢复 RAW10
格式、V4L2 转换和帧大小计算，并包含 2026-08-28 会话
`codex__01a04890-0a5c-76c0-9793-d1b8f9194eb6.jsonl` 第 228 行的
`IMGDATA_SET_BUF` 错误返回修复。

`patches/nuttx-mipi-dsi-messages-recovered.patch` 从 2026-08-19 会话
`codex__01a01a8e-73d4-7593-a65c-38aa605c773c.jsonl` 第 104、431、439 行
恢复 MIPI DSI 消息清零及 LPM 模式传递。它修复运行期行为，不应因为编译
未报错而漏掉。以上恢复均只提取公共源码相关部分，不回退已维护的应用/BSP。

## 当前可直接构建

在工作区根目录执行，不需要重新同步或重新应用公共补丁：

```bash
./build.sh vendor/openvela/boards/contest2026_284_board/configs/nsh -j4
./build.sh vendor/openvela/boards/contest2026_284_board/configs/velapoka -j4
```

切换配置会清理上套配置的中间产物，应先保存 `nuttx/vela_nuttx.bin`、
`nuttx/nuttx` 和 `nuttx/.config`。`build.sh` 还会执行 `savedefconfig` 并回写
维护仓的 defconfig，构建后应检查差异。

首次构建需联网拉取 ESP HAL、mbedTLS；产品还需 ESP-Hosted、protobuf-c。
`local/esp-hal-3rdparty` 是清理前保留的缓存（带旧修改），不要在其中 reset。
不建议直接把 `ESP_HAL_3RDPARTY_URL` 改为本地缓存路径：HAL 的 mbedTLS
子模块使用相对 URL，本地 origin 会使该 URL 解析到不存在的本地目录。
本次曾遇到此问题，已把新生成的 HAL 副本 origin 恢复为公共 GitHub URL，
同步子模块 URL、补全 mbedTLS 并完成兼容补丁；原缓存未改动。
不要把唯一缓存放进 `nxtmpdir`：默认 configure 会删除该临时目录。

运行映射如下（均为符号链接）：

```text
contest2026_284_zhuanqianmaibanzi -> contest284-manifest
vendor/openvela/boards/contest2026_284_board -> ../../../contest284-manifest/board/contest_board
apps/examples/camtest -> ../../contest284-manifest/app/camtest
apps/examples/velapoka -> ../../contest284-manifest/app/velapoka
prebuilts -> openvela-prebuilts-kept
build.sh -> nuttx/tools/build.sh
```

## 再次丢失公共源码时

先备份维护仓未提交修改和工具链。以下步骤用于公共工作树已清理的情况，
不要对已有脏公共仓强制同步或重复打补丁。

```bash
repo init -u https://github.com/open-vela/contest2026_284_zhuanqianmaibanzi.git \
  -b dev-ai-contest-2026 -m contest2026_284_zhuanqianmaibanzi.xml \
  --depth=1 --no-clone-bundle \
  --repo-url=https://github.com/aosp-mirror/tools_repo.git
cp contest284-manifest/tools/build-env/manifest.lock.xml .repo/manifests/recovery-locked.xml
```

在 `.repo/local_manifests/local-maintenance.xml` 中保留下列内容，防止 repo
覆盖维护仓以及已保留的工具目录：

```xml
<manifest>
  <remove-project name="contest2026_284_zhuanqianmaibanzi"/>
  <extend-project name="prebuilts_clang_linux_wasm" groups="notdefault,local-cache"/>
  <extend-project name="prebuilts_emulator_skins" groups="notdefault,local-cache"/>
  <extend-project name="prebuilts_emulator_tools" groups="notdefault,local-cache"/>
  <extend-project name="prebuilts_tools" groups="notdefault,local-cache"/>
</manifest>
```

```bash
repo init -u https://github.com/open-vela/contest2026_284_zhuanqianmaibanzi.git \
  -b dev-ai-contest-2026 -m recovery-locked.xml --depth=1 --no-clone-bundle \
  --repo-url=https://github.com/aosp-mirror/tools_repo.git
repo sync -c -j6 --fail-fast --no-clone-bundle --no-tags
```

**等待同步成功后**再配置；同步未完成时，可能暂缺 TypeScript 的
`ts2wasm/Make.defs` 等必需文件。恢复上述运行映射，然后仅对未打补丁的
固定 NuttX 基线执行：

```bash
for patch in \
  contest284-manifest/tools/build-env/patches/nuttx-pr{371,372}.patch \
  contest284-manifest/tools/build-env/patches/nuttx-esp32p4-spi-recovered.patch \
  contest284-manifest/tools/build-env/patches/nuttx-esp32p4-hr-timer-recovered.patch \
  contest284-manifest/tools/build-env/patches/nuttx-raw10-capture-recovered.patch \
  contest284-manifest/tools/build-env/patches/nuttx-mipi-dsi-messages-recovered.patch \
  contest284-manifest/board/contest_board/upstream/nuttx/0003-mmcsd-spi-verify-write-completion.patch; do
  git -C nuttx apply --check "../$patch" && git -C nuttx apply "../$patch" || break
done
```

公共补丁保留为 NuttX 工作树修改，不虚构已经合入上游。
检查已应用状态可使用 `git -C nuttx apply --reverse --check ../补丁路径`。

本次原始残留和两套原始 defconfig 备份在
`recovery-backup/2026-10-24/`。同步和构建日志现已归档到工作区
`logs/build-env/2026-10-24/workspace-restore-*`，文件名和内容不变。
目录用途、旧副本归档位置与可选 IDE 入口见 [工作区布局](workspace-layout.md)。

以后释放空间时，应先归档有效镜像和日志，再清理可再生中间产物。
不要把 `.repo`、`nuttx`、`apps`、`build` 当作单纯的编译输出目录删除；
尤其公共工作树中的未提交驱动改动，应先导出补丁并纳入维护仓。
