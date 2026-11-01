# 独立供电、触屏数据管理与 Windows 上传验收

本次新增功能为软件候选；不得继承 M4/M5 的实板结论标记为已验收。

2026-10-31 后续候选已调整退出逻辑，并修复视频中的 Windows 接收端兼容问题。
新版本请优先按 [触屏流程修复说明](touch-session-fix-2026-10-31.md) 验收：
主界面 `Finish` 保留 Data，Data 中的 `Safe exit` 才执行真正安全清理；完成页
可重新开始或打开数据。下文 Exit 行为描述属于 10-24 首版，不再作为新版操作入口。

2026-10-24 已恢复完整环境，NSH 和产品目标构建均通过。候选镜像位于维护仓
`out/firmware/2026-10-24/velapoka/vela_nuttx.bin`，848880 B，SHA-256 为
`38bb12c7d4a620f7890d6b78bfd371deba77d232d1a02b2447dd99cdee4dc199`。
恢复与构建记录见 [构建环境说明](../../../tools/build-env/README.md)，
本次尚未烧录或增加实板证据。

## 配置与文件

- 设备新增 `Data / Upload` 页，显示实际 `eth0` 的 IP、掩码、MAC、载波状态，
  HTTP 监听状态、Windows 最近握手结果、FAT 总/已用/可用容量和写盘队列。
- `velapoka_upload.c` 新增优先级 80、8 KiB 栈的后台线程，UI 只读取状态快照，
  不执行 SD 读写、connect 或传输。容量/网口每约 2 秒更新，页面每约 500 ms 更新。
- 新增导出会话固定 JSONL 长度，根据其中图片路径导出，最后发送同一 JSONL 前缀。
  存储锁只覆盖局部读操作，网络等待不持有存储锁。
- 未启用/异常的 ESP-Hosted Wi-Fi 不再因返回负值阻断触屏产品自启动。
- `configs/nsh` 仍为维护基线；现有 Exit 仍退出应用到 NSH，并非软件断电。

Windows 使用方式见 [接收程序说明](../../../tools/windows-receiver/README.md)。

## 实板操作

1. 使用本次归档的产品候选，或重新构建 `configs/velapoka` 后烧录。接收端默认
   `10.0.0.1:8899`，设备 `10.0.0.2/24`。
2. 拔除 PC USB 和 USB-UART，只使用外部电源给主板/屏幕供电。冷启动应自动
   进入应用，恢复模板、阈值与历史。
3. 通过触屏完成录入、PASS/FAIL，打开数据页。和 PC 读卡对照 SD 总/可用容量，
   确认超过 4 GiB 的卡不会整数溢出；Pending saves 排空后记录数应增长。
4. 不插网线时应显示 NO CABLE；插线并完成协商后显示 LINK UP。设备 IP、
   掩码、MAC 应和 NSH 维护命令一致。页面正常情况下无需接串口。
5. Windows 设置有线 IP，启动接收 `.exe` 并允许入站规则。设备 Test PC 通过。
6. 点击 Upload，核对 PC 目录出现 JSONL 和其引用的所有 FAIL BMP；长度和 CRC
   正确，屏幕显示 PC confirmed saved。重复上传应跳过相同文件。
7. 上传同时继续录入/检测，预览和触摸保持响应。新记录下次上传可见，无文件
   错配、缺图或编号跳回。尚未写盘的记录不应被误报为本批完成。
8. 传输时拔网线、关闭 PC 接收端、接收目录设为不可写，分别确认失败状态；
   SD 原件和 PC 上原有完整文件保留。恢复后点击 Retry 应成功。
9. 上传过程中执行安全 Exit，应及时取消网络等待、关闭导出文件，再排空存储
   队列、卸载 FAT。接收端不能把残留 `.part` 当作已完成文件。
10. 重启 Windows 接收程序应恢复目录/IP/端口配置；无需浏览器、Python或串口命令。

## 协议 VKP1

专用 TCP 8899，不改变旧 HTTP 8080。每条控制行以 ASCII LF 结束：

```text
device → VKP1 <12位小写MAC>\n
PC     → OK\n
device → PING\n                      # 扫描/计算 CRC 时保活，可重复
PC     → OK\n
device → FILE <相对路径> <十进制长度> <8位CRC32>\n
PC     → [WAIT\n ...] HAVE\n          # 已存在相同文件
       或 [WAIT\n ...] SEND\n
device → <恰好 length 字节文件内容>   # 只有 SEND 才发送
PC     → OK\n                        # CRC/刷盘/替换成功
device → DONE\n
PC     → OK\n
```

CRC 为标准 reflected CRC32/IEEE，初值 `0xffffffff`，多项式 `0xedb88320`，
最终取反。只允许 `results.jsonl`、`fail/fail_XXXXXXXX.bmp`；拒绝目录穿越、
配置和模板路径。PC 校验已有大文件时通过 WAIT 保持设备等待；设备扫描长日志
和计算 CRC 时通过 PING 保持连接。设备 poll 每 100 ms 检查退出，空闲超时约
3 秒；PC 接收等待 5 秒。大批量仍需实板吞吐与 SD 并发回归。
