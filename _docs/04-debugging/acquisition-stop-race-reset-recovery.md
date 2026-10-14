---
id: LMEF-DBG-003
title: 采集快速停止竞态、复位恢复与正常停止误判
document_type: debugging
module: acquisition-workflow
status: fixed-and-built-pending-runtime-verification
created: 2026-10-12
updated: 2026-10-14
code_refs:
  - motorbuffer/all.prg
  - src/acquisition/DataAcquisitionService.cpp
  - src/acquisition/DataAcquisitionService.h
  - src/workflow/TestExecutionService.cpp
---

# 采集快速停止竞态、复位恢复与正常停止误判

## 现场现象

2026-10-10 的现场日志记录到以下故障链：

1. 新一轮采集刚启动后立即停止；
2. 上位机已经写入 `DCSTART_CON=0`，但持续读到 `DC_ACTIVE_BLOCK=1`、`DC_BLOCK_SEQUENCE=0`；
3. 停止排空超过 5 秒，采集服务进入 `Fault`；
4. 控制器复位完成后机器状态回到空闲，但采集服务仍为 `Fault`，下一次启动被旧错误拒绝；
5. 正常停止状态 `G_STATE=-2`、`G_ERROR_CODE=2001` 被流程层错误升级为机器故障。

修复前基线通过 annotated tag `bug-acquisition-stop-race-ea050cd` 标记，指向提交 `ea050cd`。快速停止竞态的 Buffer 修复由 annotated tag `fix-acquisition-stop-race-20261014` 标记。两项 C++ 修复见下文，属于工作区中的其他改动，不包含在这个快速停止竞态修复提交中。同一基线还可复现停止排空时重复读取 `sequence=1` 的问题，证据和后续修复见 [LMEF-DBG-005](acquisition-duplicate-tail-block-fixed-at-ea050cd.md)。

## 一、快速停止竞态

### 已确认时序

现场日志的关键时间线为：

```text
16:52:55.036  control=1 active=0 published=1
16:52:55.056  上位机成功写入 DCSTART_CON=0
16:52:55.062  control=0 active=1 published=0
```

`WHILE DCSTART_CON` 判断成立后，到 `DC` 真正激活前存在一个很窄的窗口。如果 `DCSTART_CON` 在该窗口变为 0，`ON ^DCSTART_CON` 会被触发，但此时 `S_ST.#DC` 仍可能为 0，事件处理程序不会执行 `STOPDC`。主流程随后继续执行已经进入的循环体并启动 `DC`，而控制变量已经保持为 0，不会再产生新的下降沿。

### 修复

仅修改现场使用的 `motorbuffer/all.prg`。保留原有下降沿停止处理，并在每条 `DC` 指令完成启动后再次进行电平检查：

```text
IF ^DCSTART_CON & S_ST.#DC
    STOPDC
END
```

这样停止发生在 `DC` 激活之后时仍由原事件处理程序负责；停止发生在循环判断之后、`DC` 激活之前时，则由 `DC` 后的电平复查补偿。

## 二、控制器复位后采集服务仍为 Fault

### 原因

控制器复位会停止并重新启动 ACS Buffer 1、2、3，但 `DataAcquisitionService` 原先只会在 ACS 重新连接时把 `Fault` 改回 `Idle`。同一连接内的 Buffer 复位不会触发重新连接，因此：

```text
控制器/Buffer：已复位
机器状态：空闲
采集服务：仍为 Fault
```

启动前置检查随后返回上一次保存的采集错误，且不会创建新的执行编号或实验记录。

### 修复

`DataAcquisitionService` 直接监听共享 `AcsClient` 的 `controllerResetCompleted` 信号。只有当前采集状态为 `Fault` 时才执行以下恢复：

- 停止残留的元数据轮询；
- 失效停止超时计时器；
- 清零上位消费序号；
- 将采集状态恢复为 `Idle`；
- 重新发布采集就绪状态。

采集模块自己负责恢复采集状态，不把该职责放到 UI 或任务流程模块。

## 三、正常停止被升级为故障

下位程序把 `G_STATE=-2`、`G_ERROR_CODE=2001` 定义为“收到正常停止请求”，但流程层曾使用 `state < 0` 统一进入故障处理。修复后优先识别 `-2/2001` 并按正常停止回执处理；其他负状态仍进入原故障锁存流程。

该判断不依赖上位任务上下文是否仍存在，因为运动减速完成信号可能晚于采集和数据库收尾。即使任务上下文已经清理，迟到的正常停止回执也不应重新锁存机器故障。

## 验证范围

静态检查与既有 Stage 1 结果：

- 此前 C++ 工程使用 `vs2022-x64-debug` preset 编译通过；本次独立提交未重新编译；
- `motorbuffer/all.prg` 的修复块位于 `DC` 启动之后、`TILL ^S_ST.#DC` 之前；
- `bug-acquisition-stop-race-ea050cd` 指向修复前提交 `ea050cd`；`fix-acquisition-stop-race-20261014` 指向快速停止竞态的独立修复提交。

仍需单独确认后才能进行的 Stage 2 现场验证：

1. 新一轮采集启动后立即停止，确认 `DC_ACTIVE_BLOCK` 能及时回到 0；
2. 制造一次采集故障并执行控制器复位，确认采集状态恢复为 `Idle` 且可以再次启动；
3. 执行正常停止，确认 `-2/2001` 不会使机器进入故障状态。
