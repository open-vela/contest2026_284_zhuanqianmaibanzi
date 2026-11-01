# 主机验证

这些测试编译真实应用源码，替换板级网卡、挂载检查和 framebuffer ioctl 环境，
不替代 ESP32-P4 固件构建或实板验收。

## C 上传与 C# 接收端联调

resilient 版覆盖 27 项：新增代理注入永久/瞬时 CRC 错误、确认丢失重连、缺图
继续及旧 JSONL 保护、失败日志跨重入及写失败、瞬时 SD 读取错误、退避取消、
路径错误终止、实际 3 秒 io_lock 超时与 WAIT 总期限。最后一项仅主机编译时
把 120 秒上限缩短为 500 ms；目标固件保留 120 秒。所有注入均不进入目标配置。
小 BMP 缓存使每 4 KiB 延迟 400 ms 的模拟首次上传从约 11 秒降到约 6 秒；
这是主机延迟模型，不是 ESP32-P4 实测提速值。
PC 还会拒绝尚未确认 results.jsonl 却声称完整的批次汇总。
额外验证 64 KiB 缓存分配失败后按原 4 KiB 流程完成上传；慢 BMP 缓存路径
按 read 调用次数确认只有一次预读，而不是用主机运行时间猜测读盘次数。

需要 Linux GCC、Python 3 和 Mono（`mono`、`mcs.exe`）：

```sh
python3 tests/host/test_upload.py --mono mono --mcs /usr/lib/mono/4.5/mcs.exe -v
```

覆盖真实 C 存储写盘、冻结快照、CRC/分块上传、重复文件、损坏/中断保护、路径与
长度白名单、JSONL 版本备份、5,100,283 B 文件、静默接收端取消、接收端缺席。
上传超时回归还包括：58,678 B BMP 每 4 KiB 读取延迟 400 ms，配合严格 5 秒
超时的真实 C# 接收端验证设备保活及重复跳过；原始文件数据暂停 5.5 秒；
PC 确认延迟 5.5 秒；慢校验取消；有限空闲超时清理临时文件并输出阶段日志。
`upload_slow_io.c` 只在主机链接时包装应用 `read()`，默认不延迟，测试通过
`VKP_TEST_READ_DELAY_US` 单独开启，不进入目标固件。
测试 TCP 服务只监听 `127.0.0.1` 随机端口；受限环境需要允许本机回环 socket。
不会安装组件、配置网卡或修改防火墙。测试生成的文件仅位于临时目录。

## 页面编译与渲染

新增检查：结果停留两秒恢复 LIVE 并清除红框、保留结果；显式冻结可取消旧
恢复定时器；INCOMPLETE 的失败/重试/耗时/速率和清单提示不覆盖按钮。

优先使用工作区实际固定的 openvela LVGL 分支（头部版本为 9.1.0，含后续 API
回移）。早期 9.2.2 主机截图不能代替实际分支回归。下面从维护仓目录运行：

```sh
cmake -S ../apps/graphics/lvgl/lvgl -B /tmp/vkp-ui/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLV_CONF_PATH="$PWD/tests/host/lv_conf.h" \
  -DLV_CONF_BUILD_DISABLE_EXAMPLES=ON \
  -DLV_CONF_BUILD_DISABLE_DEMOS=ON \
  -DLV_CONF_BUILD_DISABLE_THORVG_INTERNAL=ON
cmake --build /tmp/vkp-ui/build -j2
gcc -std=gnu11 -Wall -Wextra -Werror \
  -DLV_CONF_PATH="$PWD/tests/host/lv_conf.h" \
  -Itests/host/include -Iapp/velapoka -I../apps/graphics/lvgl \
  tests/host/ui_preview.c app/velapoka/velapoka_ui.c \
  /tmp/vkp-ui/build/lib/liblvgl.a -lm -pthread -o /tmp/vkp-ui/preview
/tmp/vkp-ui/preview /tmp/vkp-ui/data-page.ppm
```

主机测试配置的接收端为 `127.0.0.1`，设备产品 Kconfig 默认为 `10.0.0.1`。
截图展示模拟数据，用于检查控件和排版，不能据此声称 SD/PHY 已实板通过。

2026-10-31 页面测试增加：冻结结果帧、PASS 隐藏红框、ROI 边界裁剪、空框/
越界框/异常 box_count、Data 页面打开时不改写预览，以及安全退出页按钮、
三次 LVGL/UI 创建销毁。按钮测试只验证动作派发，不模拟真实相机关闭或 SD
重新挂载；完整资源生命周期仍需实板重复三次 `Safe exit → Open data/Restart`。
上传失败页还检查 Stage/文件/错误信息存在，并且不覆盖底部操作按钮。
