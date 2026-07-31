---
id: LMEF-DBG-001
title: 模态 QProgressDialog 更新进度时事件重入
document_type: debugging
module: experiment-log
status: fixed-pending-runtime-verification
created: 2026-07-28
updated: 2026-07-28
code_refs:
  - src/view/pages/RunRecordsPage.cpp
  - src/view/pages/RunRecordsPage.h
  - src/experimentlog/ExperimentExportService.cpp
---

# 模态 QProgressDialog 更新进度时事件重入

## 问题现象

Excel 导出完成后，程序再次进入 `RunRecordsPage::handleExportProgress(int)`，并报告读取访问权限冲突。Visual Studio 将异常位置指向：

```cpp
exportProgressDialog_->setLabelText(...);
```

调试器中被调用对象的 `this` 接近空地址（例如 `0x8`）。

## 预期行为

导出进度到达完成状态后关闭模态进度对话框，随后显示导出成功消息，不再访问已经结束生命周期的进度界面。

## 原因分析

原实现存在以下事件顺序：

1. 导出工作线程连续发送 `progressChanged(100)` 和 `completed(targetPath)`。
2. GUI 线程进入 `handleExportProgress(100)`，先调用模态 `QProgressDialog::setValue(100)`。
3. 模态进度对话框的 `setValue()` 可能调用事件处理，使排队的 `completed` 信号在当前进度槽尚未返回时被处理。
4. `handleExportCompleted()` 调用 `closeExportProgressDialog()`；原实现关闭对话框、调用 `deleteLater()`，并将成员指针设为 `nullptr`。
5. 控制流回到尚未结束的 `handleExportProgress()`，继续执行 `setLabelText()`，此时成员指针已经为空或对象正在等待销毁，因而发生访问冲突。

这也解释了为什么 Visual Studio 指向 `setLabelText()`：真正导致指针状态变化的是前一行 `setValue()` 引发的嵌套事件处理，但非法访问发生在控制流恢复后的下一次对象调用。

## 修复方案

采用单一完成路径，并避免在事件重入期间销毁进度对话框：

- 工作线程完成时只发送 `completed`，不再额外发送最终的 `progressChanged(100)`。
- `handleExportCompleted()` 负责设置完成文案和最终进度值，然后隐藏对话框。
- 普通进度处理中先更新文案，将可能处理事件的 `setValue()` 放到函数最后，调用后不再访问对话框。
- 进度对话框由 `RunRecordsPage` 创建一次并复用；关闭导出界面时只隐藏，由父对象在页面销毁时统一释放。
- 保留不可取消、窗口模态、禁用自动关闭和自动重置的原有交互要求。

## 影响范围

修复只改变实验记录 Excel 导出的进度展示及完成信号衔接，不改变数据库读取、Excel 内容、目标路径或文件保存方式。

## 验证状态

- 静态根因分析：完成。
- 代码修复：完成。


运行验证应至少覆盖单条实验导出和汇总导出，并确认：

1. 进度达到完成状态时不再崩溃。
2. 完成消息只显示一次。
3. 连续执行第二次导出时，复用的进度对话框能够从 0 正常显示。
4. 导出失败时进度对话框能够隐藏，下一次导出仍可正常启动。
