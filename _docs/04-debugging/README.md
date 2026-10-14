# 调试

## 职能

只说明代码为何没有按照预定逻辑执行，包括目标、预期与实际结果、问题及原因。

## 文档索引

在此登记本分类下的主题文档、状态和关联内容。

- [LMEF-DBG-001：模态 QProgressDialog 更新进度时事件重入](qprogressdialog-setvalue-reentrancy.md) — 已修复，待运行验证
- [LMEF-DBG-004：启停采集会话基线错误与尾块重复读取](acquisition-session-handshake-and-overlapping-reads.md) — 已实现会话握手、读取防重和旧回调隔离；含两份日志、偶发条件及零点关联分析，待运行验证
