# 2026-11-01 整理上库改动清单

本次汇总自 `3942e41` 之后的工作区修改，包含 10-24 环境恢复和独立展示、
10-31 触屏/上传修复、resilient 容错增强及工作区清理记录。
目标分支为作品仓 `dev-ai-contest-2026`，不替公共依赖仓提交或推送修改。

## 功能和修复

- **独立展示**：新增 Data / Upload 触屏页，显示 eth0 的链路、IP、掩码、MAC，
  SD 总/已用/可用容量及留档状态；提供 Test PC、一键上传和进度/错误信息。
  Wi-Fi 非关键初始化失败不阻断以太网离线产品启动。
- **无需浏览器的 Windows 接收端**：新增 .NET Framework 窗口、保存目录设置、
  状态日志及 Start.cmd/Start.ps1。源码可在 Windows 本地编译启动，不要求 Python。
- **触屏流程**：Finish 停止检测展示但保留 Data/上传；Safe exit 才依次停止服务、
  保存并卸载 SD。结束页保留 Restart/Open data 操作，重新创建应用资源。
- **显示与检测结果**：Data 页不透明且不重绘被遮挡预览；结果关联实际检测帧，
  仅 FAIL 绘制裁剪到 ROI 的红框；停留两秒自动恢复 LIVE，保留结果指标。
  显式 Stop、下一次操作和退出会取消旧恢复定时器。
- **上传超时**：慢 SD 预校验改为按时间保活；增加有限连接/I/O 等待、取消检查及
  设备/PC 阶段日志，修复小于 64 KiB 的 BMP 校验期间可能无保活的问题。
- **上传容错**：CRC32/长度校验、未变化跳过、PC 临时文件和原子替换；单文件
  最多三次尝试。局部图片错误记日志后继续，持续网络故障终止批次。存在缺图时
  显示 INCOMPLETE，不发布新的正式 results.jsonl，保留 PC 旧清单。
  SD 与 PC 分别保留失败/批次历史，新固件与新 PC 使用 VKP2 协议配套。
- **速度候选**：不超过 64 KiB 的小文件缓存减少重复读盘，分配失败退回分块路径；
  产品默认 SPI 仍为 1 MHz，新增独立 `configs/velapoka_spi4m`，不宣称 4 MHz
  已稳定，也不把模拟读盘耗时作为实板传输速度。

PASS 和 FAIL 都在 JSONL 中；只有 FAIL 生成并上传 BMP。
上传不删除 SD 原件，正在写入或快照之后的新记录留待下次同步。

## 构建环境与工作区

- 新增锁定公共源码清单、恢复说明及七份 NuttX 恢复补丁，涵盖 P4 架构公共 PR、
  USRSOCK、MMC/SD、SPI2、定时器、RAW10 和 MIPI-DSI 历史修复。
  补丁重放顺序和 MMC/SD 校正版选择以 `tools/build-env/README.md` 为准。
- 新增可选 VS Code 多目录工作区、产品/NSH 构建任务、目录和归档导航。
- 构建生成的 `common/board` 绝对路径链接取消跟踪，本地保留，后续由 NuttX
  自动生成；忽略主机字节码缓存和既有输出目录。
- 之前两份失效验证树已完整压缩并校验后移除展开副本，约净释放 547 MiB，
  可由本地 `recovery-backup/archives/` 恢复。源码、工具链、公共构建树、
  AI/比赛材料和回退固件未清理，详见 `tools/build-env/workspace-layout.md`。

## 验证与交付边界

- 10-31：1 MHz/4 MHz ESP32-P4 目标构建和镜像校验通过；NSH 恢复构建已有
  10-24 记录。11-01 为整理提交，不重新构建或烧录。
- 11-01 提交前重新运行真实 C/C# 上传联调、实际固定 LVGL 9.1.0 分支页面/定时器
  回归与应用 checkpatch；结果见 `../validation/2026-11-01-repository.txt`。
- 用户已反馈上一版上传正常、Data 暂未再花屏；不将其等同于 resilient 新容错、
  Safe exit 资源重入或 4 MHz 候选的完整硬件验收。Windows GUI 未在本机执行。
- Git 只交付源码、配置、测试、说明及验证记录；本地 `out/` 固件、ELF、EXE、ZIP、
  工具链、公共依赖目录和旧验证树压缩包均不上传。本次不修改公共 PR 或创建 release。
- 最新本地 1 MHz 镜像 SHA-256：
  `bd4cc6c8c188b23cab85804424d7f146b00ec44f8e2a37e8741b4f3d7b2a5698`。
  全套本地交付包为 `out/VelaPoka-Resilient-2026-10-31.zip`，SHA-256：
  `ba90fe1cb06b937bc135d0d8b0ded22f639b163a1c8925b56dd18cac6bdb7575`。

网络接收仅面向受控局域网，无 TLS/用户认证。有限超时不能强制中断底层驱动
永久阻塞的 read/statfs/fsync；原子替换保证单文件而非整批事务。
下一步仍是外部供电、Windows 接收与检测并发、断线/坏图、三次安全退出重入，
以及备份测试卡上的 4 MHz 读写 CRC/断电/冷启动验收。
