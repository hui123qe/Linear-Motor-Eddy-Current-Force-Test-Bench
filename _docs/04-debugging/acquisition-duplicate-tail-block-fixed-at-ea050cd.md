---
id: LMEF-DBG-005
title: ea050cd 停止排空时重复读取尾块及其修复覆盖
document_type: debugging
module: acquisition-workflow
status: implementation-reviewed-pending-runtime-verification
created: 2026-10-14
updated: 2026-10-14
code_refs:
  - src/acquisition/DataAcquisitionService.cpp
  - src/acquisition/DataAcquisitionService.h
  - src/motion/AcsClient.cpp
  - src/motion/AcsClient.h
  - motorbuffer/all.prg
---

# ea050cd 停止排空时重复读取尾块及其修复覆盖

## 结论与证据范围

`bug-acquisition-stop-race-ea050cd` 指向的提交 `ea050cd92552261d6174f5e4f5e6b827714e37e3` 还存在一个独立缺陷：停止排空时，**同一会话的 `sequence=1` 尾块被上位机请求两次**。第一次块回调把 `lastConsumedSequence_` 从 0 推进到 1；第二次收到相同的块 1 后，严格连续性检查报告“期望 2，收到 1”。这份日志没有显示控制器漏发序号 2。

此缺陷的直接根因已由 `7b47445` 的读取在途保护覆盖，并由 `639c8bc` 的显式阶段重构保留；当前工作区仍保留该保护。它是**源码层面已修复、运行回归待验证**，不应写成现场已验证通过。`ea050cd` 上的另一种快速停止后控制器仍处于活动块的竞态，见 [LMEF-DBG-003](acquisition-stop-race-reset-recovery.md)；下一轮采集沿用旧序号基线的问题，见 [LMEF-DBG-004](acquisition-session-handshake-and-overlapping-reads.md)。三者的触发和直接原因不同。

两篇针对本次事件的文档共用 [13:50 原日志摘录](evidence/acquisition-duplicate-tail-block/2026-10-13-1350-stop-tail-and-callback-delay.log)，另一篇是 [回调变慢分析](acquisition-callback-delay-at-ea050cd.md)。摘录仅保留原日志中 `2026-10-13 13:50:35.*` 至 `13:50:50.*` 的完整行，保持每行原文和先后顺序。原文件：`D:/doc/WXWork/1688857638618363/Cache/File/2026-10/高速涡流测试台-2026-10-13.log`，大小 13,640,409 字节，SHA-256 `70B6BF7C5A1641FD0CADD0CAFA37D9F191A2F471D39CCC2C9A0EFB2A7FA3BEB9`。日志里的源码行号属于现场 `ea050cd` 构建，不是当前行号。

## 故障链

| 时间 | 原日志事实 | 含义 |
|---|---|---|
| 13:50:42.832 | 从 Collecting 进入 Stopping，同时启动停止轮询并请求写 `DCSTART_CON=0` | 停止定时器与控制写入回执均可促发查询 |
| 13:50:43.892–43.897 | 定时器触发一次元数据查询；ACS 读出 `control=0, active=0, published=1` | 第一份尾块快照已在返回途中 |
| 13:50:44.880–44.884 | 主线程处理关闭写入回执，又入队一次元数据查询；ACS 读出同样的尾块快照 | 两份元数据结果并存，停止定时器无法撤销已触发的请求 |
| 13:50:44.918 | 第一份尾块元数据被处理，`lastConsumed=0`，入队 `block=1, expected=1` | 首次读块 |
| 13:50:44.994 | ACS 读块、校验与转换完成，准备发出 `sequence=1` 回调 | 读取本身约 75 ms |
| 13:50:46.932 | 主线程先处理第二份元数据，此时仍 `lastConsumed=0`，再次入队 `block=1, expected=1`；随后才消费第一份块回调 | 缺少在途保护使同一逻辑块被请求两次 |
| 13:50:49.001 | 第二份块 1 回调到达，`lastConsumed=1`，严格检查报“期望 2，收到 1” | 是重复请求的后果，并非已证实的缺块 |

`AcsClient` 在当时对每次物理块读取都分别做了前后稳定性检查。两次读取同一块且块尚未复用时，两次均可合法返回 `sequence=1`；单块稳定性检查不能防止上位机重复发起请求。

## ea050cd 的根本原因

`DataAcquisitionService::stopCollection()` 在旧版本同时启动 50 ms 单次轮询并写入关闭命令；`onCollectionControlWritten(false)` 还会立刻调用 `requestMetadata()`。旧 `requestMetadata()` 只停止尚未触发的定时器，没有记录“元数据请求已入队/正在等主线程回调”；旧 `requestBlock()` 也没有记录“这个块已在等回调”。

请求经 Qt 队列进入 ACS 线程，结果再经队列回到主线程。**ACS 已完成读取，不等于主线程已推进 `lastConsumedSequence_`。** 当第二份元数据比第一份块回调先被主线程处理时，两次选块都从本地的 0 推导出 `expected=1`。这是请求调度重叠，不是序号比较条件错误。单纯忽略第二次 `sequence=1` 会掩盖重复入队，还可能遮蔽真正的跨会话迟到回调。

## 历次提交如何覆盖这一故障

| 提交 | 实际作用 |
|---|---|
| `ea050cd` | 故障基线；停止轮询和关闭回执可以各自入队元数据，元数据及块读取没有未完成标记 |
| `7b47445` | 首次加入元数据、块、控制写入的在途标记；从请求入队一直保留到主线程处理匹配回调；加入 `generation` 隔离旧会话结果和控制器会话握手 |
| `639c8bc` | 把多个布尔标记整理为 `PendingRead::None/Metadata/Block`、`PendingControl::None/Enable/Disable` 和显式 `Phase`；保留单读取链与回调后再查询的行为 |
| 当前未提交工作区 | 仍有上述读取保护；另有复位恢复、安全状态及 UI 相关修改，不能据此推断本问题已经做过现场回归 |

当前实现中的关键约束如下，以下位置应以工作区源码为准：

1. `requestMetadata()` 检查 `pendingRead_` 和 `pendingControl_`。已有元数据、块读取或控制写入未完成时，新的查询触发被合并；真正入队前设置 `PendingRead::Metadata`。见 `src/acquisition/DataAcquisitionService.cpp` 的 `requestMetadata()`。
2. `onMetadataRead()` 只接受当前 `generation` 且仍处于 `PendingRead::Metadata` 的结果。关闭命令正在等待回执时收到的旧快照被放弃，由关闭回执请求新快照。因此同一会话不会并行建立两条选块链。
3. `requestBlock()` 使用同一套读取保护，先设置 `PendingRead::Block`，再把块请求排入 ACS 线程。此时定时器或回执再次调用 `requestMetadata()`，只能被合并，不能再次选中 `sequence=1`。
4. `onBlockRead()` 先确认 `generation` 与 `PendingRead::Block`，保留 `block.sequence == lastConsumedSequence_ + 1` 的严格检查，推进序号并执行 `blockReady(block)`；**主线程消费及同步订阅者处理后**才清除在途标记、查询下一份元数据。ACS 发出回调时并不释放标记，正好挡住日志里的约 2 秒窗口。
5. 停止从 Collecting 进入 `Disabling`，不再同时主动启动停止轮询；关闭回执转入 `Draining` 后查询尾块。若已有读取与回执交错，在途状态仍限制只能有一条读取链。
6. 开始、结束、故障或重置时 `invalidateRequests()` 增加 `generation`；旧回调即使迟到，也不能改变新会话的在途状态或序号。配套的控制器 `DC_SESSION_REQUEST/ACK` 则解决另一类新会话序号基线问题，要求现场上位机与 Buffer 程序一同更新。

因此，按日志原有交错重放：第二份元数据被处理时，读取状态已经是 `Block`，无法再次入队块 1。第一次块回调消费完毕后，才从 `lastConsumed=1` 查询下一块；如果控制器确实没有发布 2，排空条件会按真实快照判断。严格序号检查仍能报告真正的乱序或缺块。

## 验证边界

本次是原日志、`ea050cd`、`7b47445`、`639c8bc` 与当前工作区的静态对照；没有编译、运行、模拟回调或连接设备。需要在非设备环境重放“停止轮询先触发、关闭回执再到、两份元数据早于块回调”的顺序，确认只发出一次 `block=1, expected=1`，只发布一次尾块，最后进入 Idle。再验证旧 `generation` 回调、块拒收后重查以及真实 `sequence` 跳号时仍报错。

读取防重解决的是本次**误报根因**；它不证明主线程约 1–2 秒的响应延迟已经消失。延迟可能继续影响 5 秒停止超时，见 [LMEF-DBG-006](acquisition-callback-delay-at-ea050cd.md)。
