# VelaPoka Windows 接收程序

最新版本：`2026-10-31 resilient`（窗口标题）。新增 VKP2 失败报告和全批汇总，
设备有限重试、失败后继续其他图片；缺图时不发布新 results.jsonl。
PC 的 `设备MAC/upload-journal.jsonl` 保存失败及汇总历史，设备另存
`velapoka/upload-errors.jsonl`。网络断开时 PC 可能收不到报告，SD 日志写失败
会在设备提示。日志下次成功也不清除，以最新 batch_end 判断本批状态。
请同时更新新固件和 `out/VelaPoka-Windows-Receiver-2026-10-31-resilient.zip`；
旧 PC 不支持新固件握手，新 PC 兼容旧 VKP1 固件。
详见 [容错和速度候选说明](../../board/contest_board/docs/upload-resilience-2026-10-31.md)。

以下旧版本变更保留供追溯，最新标题和行为以顶部 resilient 说明为准。

设备使用外部电源独立运行，Windows 只负责通过 RJ45 接收检测记录和 FAIL 图片。
检测、录入、阈值调整、查看容量和发起上传均在设备触屏操作。

2026-10-31 upload-fix：针对 Test PC 成功但 Upload 超时的路径，设备 CRC
校验改为按单调时间约每秒保活，不再仅每 64 KiB 保活；PC 空闲读取/发送超时
改为 30 秒，设备网络回复等待也改为 30 秒，仍保留 100 ms 取消轮询。
NuttX 无发送缓冲时的单次 send 另设 3 秒超时。SD 系统调用和等待存储锁本身
并不能被网络轮询中断，真实退出时延仍需实板验证。
设备数据页增加 `Stage`，失败后保留出错阶段；PC 增加连接、握手、重复文件和
阶段错误日志。窗口标题为 `(2026-10-31 upload-fix)`。
请同时更新固件与 `out/VelaPoka-Windows-Receiver-2026-10-31-upload-fix.zip`，
不能只刷固件或只替换 PC 程序。协议和保存目录保持兼容，SD 原始文件不删除。

2026-10-31 更新：修复预编译 EXE 在 Windows .NET Framework 上出现
`System.String.Split(Char, StringSplitOptions)` 方法不存在的问题；显式使用
字符数组重载，避免 Mono 编译时绑定到不同 API。新版窗口标题带
`(2026-10-31)`，请关闭旧程序并使用新版包。也可运行包内 `Start.cmd` 在
Windows 本机编译启动。Linux/Mono 回环测试不代替 Windows 桌面验收。

## 启动

1. 将 PC 有线网卡配置为 `10.0.0.1`，掩码 `255.255.255.0`。仅直连设备时不必
   给这张网卡设置默认网关或 DNS，PC 原有 Wi-Fi 可继续用于上网。
2. 网线连接开发板，开发板地址默认为 `10.0.0.2`。
3. 双击 `VelaPokaReceiver.exe`。源码目录使用 `Start.cmd`，它调用 Windows
   .NET Framework 4.x 自带的编译器生成并启动接收窗口，无需 Python、浏览器或
   Visual Studio。要求 Windows 桌面版安装 .NET Framework 4.x，企业策略可能
   限制脚本/程序运行。
4. 接收窗口默认自动监听 `10.0.0.1:8899`，只接受 `10.0.0.2` 发起的连接。
   如果需要修改保存目录，先点 `Stop receiving`，选择目录后再启动接收。
5. 设备右上角点击 `Data / Upload`，查看网口与 SD 信息，点击 `Test PC`，
   成功后点击 `Upload / Retry`。窗口保持运行，可最小化。

新版主界面 `Finish` 只结束检测展示，数据页与上传仍可用；它不会卸载 SD。
需要断电时，点击数据页 `Safe exit` 并等待完成，断电后再拔卡。完成页的 `Open data`
会重新初始化应用、挂载 SD 并打开数据页，`Restart` 会回到检测界面；两者都不
需要重新上电，但重新打开后 SD 已再次挂载，拔卡前须再执行安全退出。

默认保存位置为“文档\VelaPoka\设备MAC\”，也可在窗口选择本机其他普通目录。
不支持重解析点/符号链接目录。选择与程序会记在
`%LOCALAPPDATA%\VelaPokaReceiver\settings.txt`。

```text
文档\VelaPoka\001122334455\
├── results.jsonl
├── results-时间-唯一编号.jsonl    # 上次内容不同的快照备份
└── fail\
    └── fail_00000001.bmp
```

PC 窗口里的 IP/端口必须匹配设备配置。设备接收端地址由
`CONFIG_EXAMPLES_VELAPOKA_UPLOAD_HOST`（默认 `10.0.0.1`）和
`CONFIG_EXAMPLES_VELAPOKA_UPLOAD_PORT`（默认 `8899`）决定；本版屏幕只展示地址，
不提供运行期 IP 编辑。设备 IP 仍由既有 `NETWORK_IPADDR` 配置控制。

## 首次连接与防火墙

如果 Windows 提示允许程序联网，请允许连接设备的有线网络。若连接仍被阻止，
可在管理员 PowerShell 执行以下规则，只开放指定 PC/设备地址和接收端口：

```powershell
New-NetFirewallRule -DisplayName 'VelaPoka Receiver' -Direction Inbound `
  -Action Allow -Protocol TCP -LocalAddress 10.0.0.1 -LocalPort 8899 `
  -RemoteAddress 10.0.0.2 -Profile Any
```

程序不会自动改网卡或关闭防火墙。`Test PC` 成功比单独 Ping 成功更能证明接收
服务可用。屏幕 `Last PC check` 是最近一次握手结果；拔线后的状态刷新约需 2 秒。

## 上传语义

- resilient 版中，下文旧的“本批失败”更新为：单文件最多 3 次尝试；坏图/缺图
  记录后继续其他图片，有缺失则不发布新 JSONL，显示 INCOMPLETE。持续断网或
  记录格式不可信则结束本批。成功图片不回滚，旧完整 JSONL 保留。
- 不超过 64 KiB 的文件优先使用有界快照缓存，减少重复读 SD；内存不足回退
  4 KiB 分块。大文件仍两遍读取。
- 上传按钮同步“开始上传时已经写盘的 JSONL 快照”及其中引用的 FAIL 图片。
  后台队列里尚未写盘的检测和上传期间新增的记录留待下一次上传。
- 每个文件按 4 KiB 分块读取/发送，设备不将整张 SD 或整个结果文件装进内存。
  新上传通道的单文件上限为 512 MiB，与旧只读 HTTP 的 4 MiB 限制分开。
- 接收端检查文件长度与 CRC32，刷盘并替换完成后才返回成功。
- FAIL 图片先传，JSONL 快照最后传。相同文件依据长度和 CRC32 跳过；内容变化
  的 JSONL 重传完整快照，旧版本另存备份。图片内容变化也保留旧版备份。
- 中断不会用半个文件替换已保存的完整文件；未完成的临时文件在连接结束时清理。
  再次点击 Upload / Retry 会从未确认文件重新发送，不做文件内断点续传。
- SD 原始文件始终保留；本版不提供删除、格式化、上传模板或配置功能。
- 完成页的 Saved/Skipped 为本批文件计数，百分比是当前文件进度，不是整批进度。
- 相机、检测与 UI 可继续运行。安全 Exit 会先取消并等待上传线程，再停止其他
  服务、排空写盘队列和卸载 SD。
- 此协议面向设备与 PC 直连的受控局域网，使用来源 IP 限制和路径白名单，
  尚不提供 TLS 或用户认证。

## 排查

- `Test PC` 成功但上传 `-107`：连接测试不读取 SD，不能证明上传已完成。
  旧版 PC 在 5 秒收不到数据后会断开，旧版设备预读小 BMP 校验时没有保活。
  新版修复这一可复现路径，但不能据主机模拟认定实板所有 `-107` 均来自同一原因。
  如果仍失败，记录设备 `Stage`、文件名及 PC 的 `Receive failed [阶段]` 完整日志。
- `Stage: Checking file CRC` 且进度 0%：正在预读校验，尚未发送文件内容；
  `Sending file` 才进入有效数据传输，`Waiting for PC save` 在等 PC 校验落盘确认。
- `Receive failed [Waiting for FILE / PING / DONE]` 是命令阶段等待超时；
  `[Receiving 文件名]` 是该文件数据读取失败；不要直接按防火墙或 SD 空间不足处理。
- `NO CABLE`：检查网线及 PHY 协商；`UNKNOWN` 表示接口状态读取失败。
- `HTTP READY` 仅表示设备旧下载服务已监听，不等于 PC 接收窗口可用。
- `NOT READY`：确认接收窗口 READY、PC 有线 IP、端口、防火墙和设备接收端配置。
- `No saved records, or image missing`：先完成一次检测并等待 Pending saves 为 0；
  若 JSONL 引用的图片已经被人为删除，本批失败，避免上传不完整结果集。
- SD 容量来自挂载 FAT 文件系统的 `statfs()`，单位 MiB，和包装上的 GB 可能不同。
  Records/FAIL records 是已成功写盘的有效记录数，并非整个卡中所有文件数。

## 构建与测试状态

容错版 27 项回归新增：真实 C# 接收核心前的代理注入永久/瞬时 CRC 错误、确认
丢失重连、缺图继续与旧清单保护、失败日志持久化/写失败、瞬时读错、退避取消、
无效路径终止、存储锁期限、WAIT 总期限、拒绝未确认 JSONL 的完整汇总，以及缓存不足回退。
不能代替实板及 Windows GUI 验收。

C# 核心和窗口可在 .NET Framework 4.x 编译器下编译；开发时也可用 Mono 编译。
`tests/host/test_upload.py` 会把真实设备 C 存储/上传代码编译为主机测试程序，
与真实 C# 接收核心进行回环联调。模拟的是板级网卡与 FAT 挂载，不能代替实板
性能、供电、触摸和 Windows 桌面联调验收。
新增慢 SD（每 4 KiB 延迟 400 ms）、旧 5 秒超时接收端、原始数据发送间隔超过
5 秒、PC 落盘确认延迟 5.5 秒、慢校验取消和接收端超时清理/阶段日志回归。
