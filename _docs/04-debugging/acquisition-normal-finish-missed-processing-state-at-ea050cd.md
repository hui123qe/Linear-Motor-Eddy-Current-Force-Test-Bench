---
id: LMEF-DBG-007
title: ea050cd 正常结束漏读记录处理状态导致采集持续
document_type: debugging
module: acquisition-workflow-buffer
status: buffer-wait-adjusted-pending-runtime-validation
created: 2026-10-14
updated: 2026-10-14
code_refs:
  - motorbuffer/all.prg
  - src/motion/MotionControlService.cpp
  - src/workflow/TestExecutionService.cpp
  - src/acquisition/DataAcquisitionService.cpp
---

# 正常结束后采集未停止

## 目标与现场结果

每次正反向记录结束时，Buffer 将 `G_STATE` 设为 `55` 或 `75`，上位机识别该状态后停止采集并读取尾块。2026-10-13 的日志中，执行 `1791869808310` 的第 10 条记录没有被自动停止。运动正常结束后，采集继续运行约 2 分 26 秒，直到操作员发出停止请求。

原始日志：`d:\doc\WXWork\1688857638618363\Cache\File\2026-10\高速涡流测试台-2026-10-13.log`。以下均为该文件中的日志时间；行号是日志文件行号。

| 时间和行号 | 证据 |
|---|---|
| 13:37:40.468，20721–20725 | 第 9 条记录观察到 `55`，上位机立即请求停止采集；13:37:40.550 观察到 `70`，可见这次处理状态的可观察窗口约 82 ms。 |
| 13:37:46.209，21514–21517 | 第 10 条记录的运动状态由 `70` 直接变为 `60`，完成计数由 9 变为 10；日志中没有观察到 `75`，也没有自动停止请求。 |
| 13:37:47.747，21781–21782 | 运动正常完成，状态为 `100`；采集仍为 `Collecting`，`DCSTART_CON=1`。 |
| 13:40:11.992，46962–46966 | 收到人工停止请求，采集由 `Collecting` 进入 `Stopping`。 |
| 13:40:12.021，47018 | 采集排空完成；第 10 条最终保存 150438 个原始样本，整组按停止状态收尾。 |

## 原因与改动

`MotionControlService` 仅在轮询到状态变化且新状态是 `55/75` 时发送 `recordProcessingEntered`。`TestExecutionService::handleRecordProcessingEntered()` 才请求停止采集。完成计数 10 和正常完成状态 100 没有触发该请求。日志证明上位机漏读了最后一次 `75`；不能仅凭日志确定控制器状态实际持续的精确时间或哪次线程调度导致漏读。

本次只调整 `motorbuffer/all.prg` 中 `55/75` 共用的 `G_RESULT_WAIT` 默认值，从 125 改为 2000。现场此前观察到 `75→60` 约 131 ms、`55→70` 约 82 ms，与 `WAIT 125` 约 125 ms 的解释相符；因此以 `WAIT 2000` 作为约 2 秒目标。源码原注释称 125 个周期约 5 秒，与日志表现不符，已同步修正。该修改为状态轮询留出更长窗口，也会让每条记录的处理等待增加约 1.9 秒。

## 验证边界

已静态核对状态触发链、日志顺序与本次 Buffer 差异。尚未在控制器上确认 `WAIT` 的时间单位、设备是否使用该全局变量的运行时覆盖值，或验证 2 秒能否覆盖现场最长的轮询延迟。即使加长等待，若上位机连续超过窗口未轮询到状态，仍可能漏读；此改动不提供完成计数的独立停止兜底。
