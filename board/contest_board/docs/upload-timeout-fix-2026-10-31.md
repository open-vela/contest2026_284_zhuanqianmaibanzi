# 2026-10-31 上传超时修复候选

## 证据与边界

用户实板反馈：Data 页没有再出现花屏；Test PC 显示 OK；仅 Upload 报 -107。
PC 日志为读取连接超时。连接测试只完成 VKP1/OK 握手，不读取 SD。

源码确认 PC 旧空闲超时 5 秒；设备先完整预读文件计算 CRC，旧版只每累计
64 KiB 保活，而当前 FAIL BMP 为 58,678 B。主机真实 C 上传与 C# 接收核心
联调中，每 4 KiB 加 400 ms 延迟，可以复现握手正常、上传超时。
Linux 模拟返回 -104；NuttX 实板反馈 -107，不能据此声称二者所有底层事件
完全相同，也不能排除实板存在其他阻塞。修复后需用阶段日志继续实板验证。

## 改动

- CRC 扫描按 CLOCK_MONOTONIC 约每秒发送 PING/等待 OK；检查每次读盘后执行。
  JSONL 文件枚举之间也检查保活，原有每 32 行扫描的保活让步保留。
- 不在 FILE 原始内容中插入 PING，避免损坏文件。单次 SD 读取或写盘锁等待过长
  时，PC 30 秒有限空闲超时提供余量；不是无限等待，也不是隐藏错误。
- 设备网络 poll 每 100 ms 检查取消，连接最多约 3 秒，网络 I/O 等待约 30 秒。
  NuttX 无 TCP 发送缓冲时 send 本身可能等待 ACK，另设 SO_SNDTIMEO 3 秒。
  底层 SD 调用/互斥锁不因此变成可中断操作，实板退出时延须实测。
- 数据页增加 Stage，失败保留最后阶段与文件名；设备 syslog 记录阶段/错误。
  PC 日志记录连接、握手、查重、接收、刷盘/保存和失败阶段。
- PC 窗口标题带 `2026-10-31 upload-fix`；显式 String.Split 字符数组兼容修复保留。
  文件白名单、CRC、原子替换、原有文件备份、SD 原件保留和取消流程不变。

## 交付和复测

固件：`out/firmware/2026-10-31/velapoka-upload-fix/vela_nuttx.bin`。
PC：`out/VelaPoka-Windows-Receiver-2026-10-31-upload-fix.zip`。
需同时更新；旧触屏修复版固件/PC 包保留，不覆盖日期归档。

1. PC 关闭旧接收窗口，解压新包到新目录，确认标题包含 upload-fix 和 READY。
2. 按原 Windows esptool 流程烧录新固件，偏移仍为 0x2000，COM 口按实际设备选择。
3. 外部供电运行，Test PC 为 OK，再完成一条有 FAIL 图片的检测，等待 Pending saves=0。
4. Upload 应依次经过 Opening SD snapshot、Scanning SD records、Checking file CRC、
   Negotiating file、Sending file、Waiting for PC save、Confirming batch、Complete。
   校验阶段进度仍为 0%，不是传输卡死；最终 PC 需显示 Saved and verified 和 Upload complete。
5. 再上传一次，验证相同文件 Unchanged/Skipped，PC JSONL/BMP 内容可打开且与 SD 一致。
6. 上传中拔网线后恢复，重试应成功且没有半文件替换；回归 Finish、Safe exit 与重新打开数据。
7. 若仍失败，提供 Stage、文件名、错误码和本次 PC 完整阶段日志，不按单一 -107 猜测根因。

构建/主机测试结果及哈希见 `../validation/2026-10-31-upload-timeout.txt`。
主机回归不等于 ESP32-P4 或 Windows 桌面验收，本次不自动烧录开发板。
