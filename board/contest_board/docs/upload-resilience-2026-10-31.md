# 2026-10-31 容错上传、自动预览与 SPI 速度候选

## 实板基线和版本

用户已反馈上一版 upload-fix 能正常上传，Data 页短时未再出现花屏。
本版在该基线上完善容错和速度，以下新行为仍待实板验收。
Windows 标题必须包含 `2026-10-31 resilient`，新固件使用 VKP2/OK2 握手，
旧接收端在传文件前被拒绝，避免传完才发现不支持失败报告/全批汇总。
新 PC 仍兼容旧固件 VKP1；未加 TLS，仅供设备与 PC 的受控直连网络。

## 操作与容错

- Inspect 结果帧停留约 2 秒后自动恢复 LIVE，清除红框但保留 PASS/FAIL 和得分。
  不在变化的画面上继续画旧框；Stop 立即恢复。再次 Inspect、Enroll、Finish
  和 Safe exit 取消/替换旧定时器。相机采集线程不因结果暂停。
- 单文件最多 3 次尝试（首次加 2 次重试），重试前有 300/600 ms 可取消退避。
  失败连接直接关闭，重开文件并重新握手，不在剩余原始内容里插入命令。
- CRC 不匹配、可恢复读盘错误有限重试；仍失败记录后继续其他图片。
  ENOENT/非法参数/超限文件不重复尝试。
- 持续网络故障在有限重试后结束本批，不对所有剩余图片逐张等待。
  JSONL 格式/路径验证失败停止扫描，不跳过安全检查或猜测下一条边界。
- 有失败图片就不发送新 results.jsonl，不替换 PC 旧清单，显示 INCOMPLETE、
  错误/失败数/重试数/最后失败文件。没有全批原子事务：成功图片可先到达 PC，
  但不能把缺图的新记录清单宣称为完整结果。
- PASS/FAIL 记录都在 JSONL，仅 FAIL 有 BMP；本批不完整时，新 PASS 记录也
  随 JSONL 暂缓，修复后重试同步。SD 原件始终保留。
- Retry 重新枚举、查重和补传，不是文件内断点续传。已落盘但 ACK 丢失的文件
  重连后按长度/CRC 跳过，不重复计为新保存。

## 日志和等待边界

- SD：`/mnt/sdcard/velapoka/upload-errors.jsonl`，追加启动后毫秒数、文件名、
  错误码并 fsync。uptime_ms 不是 UTC，重启后可能从较小数值开始。
- PC：`保存目录/设备MAC/upload-journal.jsonl`，追加 UTC 失败报告和整批汇总。
  都是历史日志，下次成功不清除；以最新 batch_end 判断本批完整性。
  离线时 PC 可能收不到报告，以 SD 日志和设备状态为准；SD 日志写失败会明确提示。
- 容量查询、导出和失败日志等待 io_lock 约 3 秒；网络连接约 3 秒，空闲 I/O
  等待约 30 秒，单次 send 另设 3 秒期限；WAIT 不能把一个回复延长超过约
  120 秒。网络 poll 和重试退避每约 100 ms 检查取消。
- 不保证所有底层 SD 故障都可强制中断：read/statfs/fsync/驱动内部阻塞仍依赖
  底层返回，不能用取消线程强拆 FAT 锁。异常 Safe exit 时延仍需实板验证。

## 性能和两套固件

- 不超过 64 KiB 的文件使用有界快照缓存，CRC 后从同一快照发送，通常的
  58,678 B BMP 从读两遍变成一遍。额外 heap 上限 64 KiB；分配失败回退原
  4 KiB 两遍流程。大文件仍分块，不把整个文件装入 RAM。
- 页面显示本批耗时和平均已发送内容 KiB/s（包括校验/等待时间和重试字节），
  不是网口协商速率或唯一成功数据的吞吐率。
- `configs/velapoka` 保持 1 MHz，为稳定时钟基线，但包含所有新容错/缓存/预览功能。
- `configs/velapoka_spi4m` 除配置来源及 SPI 4 MHz 外与基线一致，是待实板验证
  的速度候选，不自动提为产品默认，也没有运行期自动降频。
- TCP 发送缓冲配置未改，理论 SPI 倍率不能说成实测上传倍率。

```sh
./build.sh vendor/openvela/boards/contest2026_284_board/configs/velapoka -j4
./build.sh vendor/openvela/boards/contest2026_284_board/configs/velapoka_spi4m -j4
```

产物：`out/firmware/2026-10-31/velapoka-resilient-{1mhz,4mhz}/vela_nuttx.bin`，
配套 `out/VelaPoka-Windows-Receiver-2026-10-31-resilient.zip`。旧版本保留。

## 实板验证

先更新新 PC 和 1 MHz 固件，验证两秒恢复预览、录入/检测、上传、重复跳过、
失败清单与重试恢复。模拟缺图使用备份/测试卡，不删除唯一业务图片。

再在备份测试卡试 4 MHz：对照相同记录集耗时，核对两端文件；Safe exit、完全
断电后在 Windows 读取核对，再冷启动验证历史、模板和新记录续号。重复读写、
上传并发、断线重试、退出和重入；出现新增读写/校验/持久化异常就回退 1 MHz。
未完成这些步骤，不能宣称 4 MHz 实板稳定。本次不自动烧录。

软件回归和产物哈希见 `../validation/2026-10-31-resilient.txt`。
