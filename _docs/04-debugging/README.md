# 调试

## 职能

只说明代码为何没有按照预定逻辑执行，包括目标、预期与实际结果、问题及原因。

## 文档索引

在此登记本分类下的主题文档、状态和关联内容。

- [LMEF-DBG-001：模态 QProgressDialog 更新进度时事件重入](qprogressdialog-setvalue-reentrancy.md) — 已修复，待运行验证
- [LMEF-DBG-002：采集序号不连续、故障停止重入及重新开始仍异常](acquisition-sequence-stop-reentrancy-restart.md) — 静态分析完成，待现场日志验证
- [LMEF-DBG-003：采集快速停止竞态、复位恢复与正常停止误判](acquisition-stop-race-reset-recovery.md) — 已修复并编译，待现场验证
- [LMEF-DBG-004：启停采集会话基线错误与尾块重复读取](acquisition-session-handshake-and-overlapping-reads.md) — 已实现会话握手、读取防重和旧回调隔离；含两份日志、偶发条件及零点关联分析，待运行验证
- [LMEF-DBG-005：ea050cd 停止排空时重复读取尾块及其修复覆盖](acquisition-duplicate-tail-block-fixed-at-ea050cd.md) — 同一份 13:50 日志证实重复请求 sequence=1；在途保护已覆盖根因，待运行验证
- [LMEF-DBG-006：ea050cd 采集回调约两秒延迟的证据与排查](acquisition-callback-delay-at-ea050cd.md) — 共用 13:50 日志；已定位主线程响应延迟现象，具体耗时函数待测量
- [LMEF-DBG-008：实验表重复收尾及启动失败时的终止路径重入](workflow-table-finalization-reentrancy.md) — 已统一结束表入口、明确异步状态并增加重入保护；已编译，待运行验证
