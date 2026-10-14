---
id: LMEF-DBG-006
title: ea050cd 采集回调约两秒延迟的证据与排查
document_type: debugging
module: acquisition-workflow-ui
status: evidence-reviewed-cause-unconfirmed
created: 2026-10-14
updated: 2026-10-14
code_refs:
  - src/acquisition/DataAcquisitionService.cpp
  - src/view/pages/WorkbenchPage.cpp
  - src/view/components/chart/ChartWidget.cpp
  - src/logging/AppLogger.cpp
  - src/view/DeviceStatusPanel.cpp
  - src/database/AcquisitionDatabaseService.cpp
---

# ea050cd 采集回调约两秒延迟的证据与排查

## 结论与证据

在 `ea050cd92552261d6174f5e4f5e6b827714e37e3` 的这次运行中，秒级响应延迟**早于**尾块重复读取，并同时影响采集轮询、元数据结果和数据库统计结果。它不是 ACS 矩阵读取持续两秒；主线程事件未能及时执行是更符合日志的解释。但日志未记录每个主线程槽的入口、出口与排队时间，不能把两秒全部归给某个 UI 函数，也不能排除同步日志写入、系统调度或调试暂停。

本篇与 [LMEF-DBG-005](acquisition-duplicate-tail-block-fixed-at-ea050cd.md) 共用同一份 [13:50 原日志摘录](evidence/acquisition-duplicate-tail-block/2026-10-13-1350-stop-tail-and-callback-delay.log)。原文件路径、大小和 SHA-256 记在 LMEF-DBG-005 中。日志的 `0x66a4` 是主线程、`0x764c` 是 ACS 线程、`0x7f88` 是数据库线程。下表的差值是**两条日志时间戳的墙钟差**，不是已经测得的纯 Qt 队列等待时间。

| 时间段 | 墙钟差 | 日志能证明什么 |
|---|---:|---|
| 13:50:35.519 主线程记录上一条排空完成 → 37.568 主线程再次记录单次完成信号 | 2049 ms | 此间主线程无现有日志；可能正在执行 `collectionStopped` 的同步订阅者，需测量 |
| 13:50:37.572 数据库线程记录统计完成 → 38.596 主线程记录收到统计回执 | 1024 ms | 其他线程结果也未及时由主线程处理 |
| 13:50:38.751 采集回调启动 50 ms 单次轮询 → 39.758 定时器请求实际入队 | 1007 ms（超过标称间隔约 957 ms） | 主线程定时器也明显滞后 |
| 13:50:39.763 ACS 记录元数据完成 → 40.803 主线程处理元数据 | 1040 ms | 还未进入此次尾块读取就已经延迟 |
| 13:50:40.803 再次启动 50 ms 轮询 → 42.821 定时器请求实际入队 | 2018 ms（超过标称间隔约 1968 ms） | 主线程响应出现约两秒空档 |
| 13:50:44.994 ACS 记录块 1 校验转换完成 → 46.932 主线程收到块 1 | 1938 ms | 放大了第二份元数据先于块回调被处理的窗口 |
| 13:50:47.008 ACS 记录第二份块 1 校验转换完成 → 49.001 主线程收到第二份块 1 | 1993 ms | 迟到并不是只发生一次 |

同一窗口中，ACS 第一次尾块读取约 75 ms，数据库统计查询约 3 ms。数据库 `appendBlock()` 和正常收尾通过 `Qt::QueuedConnection` 交给数据库线程，因此日志不支持“本次数据库 SQL 查询本身占住主线程两秒”这一解释。

## 更值得先测的代码路径

### 上一条记录结束时的同步图表提交

`DataAcquisitionService::finishCollection()` 在主线程发出 `collectionStopped()`。现场版本中，`TestExecutionService::handleCollectionStopped()` 提交异步数据库收尾；`WorkbenchPage::commitChartCollection()` 则直接调用 `ChartWidget::replacePoints()`。后者清空并重建曲线数据、调整坐标轴，再同步 `replot()`。日志中 13:50:35.519 记录停止完成，下一条主线程日志在 37.568；这与在 `collectionStopped()` 的某个同步订阅者内耗时约两秒**相容**。上一条记录总原始数据量是 25795 点，但位置筛选后实际传给图表的点数未被日志记录。

这是一条值得优先测量的调用链，**不是已经证实的两秒耗时**：缺少 `commitChartCollection()`、`replacePoints()` 和 `replot()` 的独立耗时记录，也不能仅凭这段时间没有主线程日志就断定它一直在画图。

### 运动期间的周期重绘

现场 `WorkbenchPage::setMotionState()` 在状态 50 或 70 启动图表定时器；`ChartWidget` 每 200 ms 调用 `scrollToLatest()`，它会调整范围并调用 `replot()`。`replot()` 使用 `QCustomPlot::rpImmediateRefresh`；仓库中的 QCustomPlot 实现会在调用线程完成布局、绘制，并调用 `QWidget::repaint()`。`scrollToLatest()` 没有在“数据未改变”时跳过重绘；鼠标十字线更新也会立即重绘。

当上一条曲线仍在图上而下一条采集尚未发布新点时，这些重绘仍可能发生。它们可以解释后续定时器和回调共同延迟的形态，但单次绘制耗时、是否连续积压以及现场 GPU/窗口状态都尚未测量。

### 同步日志和其他主线程 UI 工作

现场 `AppLogger::fileMessageHandler()` 为所有线程共用文件锁，在调用者线程写入并对每条日志 `flush()`，释放锁后还可能调用原消息处理器。日志密集时，锁等待、文件写入或原处理器都会延迟调用者。特别是 ACS 的“发送数据块回调”日志写在真正发出信号之前，因此这条日志到主线程槽入口的差值还可能包含**发送前的日志耗时**。

`DeviceStatusPanel::setSensorValue()` 在每次更新时对标签执行 `unpolish()/polish()`，属于另一个应测量的 UI 成本。已有导出相关 `BlockingQueuedConnection`，但这份故障窗口没有导出操作证据，不应把它列为本次首要原因。调试器暂停、系统负载或图形驱动异常仍是环境候选；现有日志无法支持或排除。

## 最小测量方案

优先在不连接真实设备的环境里做低开销测量；如需设备运行，应另行确定范围。对以下每项使用同一单调时钟记录耗时分布和最大值，保留慢调用的线程 ID 与操作名：

1. 主线程 `collectionStopped` 发射前后、`commitChartCollection()`、`replacePoints()`、`scrollToLatest()` 和 `replot()`；曲线绘制同时记录实际点数及 QCustomPlot 的 `replotTime()`。
2. ACS 读取完成、**实际 emit 前后**、主线程对应槽入口；这样把 SDK 读取、发送前日志和事件队列等待分开。为每笔请求带 `generation` 和请求标识，避免把两个相同序号的回调配错。
3. 日志处理器的锁等待、`write`、`flush` 与旧消息处理器耗时，以及高频传感器 UI 更新耗时。诊断数据宜先存内存环形缓冲区或低频汇总，避免逐事件同步写盘改变待测时序。

先验证 13:50:35.519 后是否确有约 2 秒图表提交，再看周期重绘是否持续造成 1–2 秒的事件迟到。若图表耗时远低于这些空档，就转向同步日志、其他 UI 槽和系统层采样。当前读取防重修复能阻断重复读块，但**不等于**消除这种响应延迟；若延迟持续，5 秒停止排空超时仍需单独回归。

本篇仅整理原日志及 `ea050cd`/当前代码的静态证据，没有新增性能埋点、运行基准或现场验证。
