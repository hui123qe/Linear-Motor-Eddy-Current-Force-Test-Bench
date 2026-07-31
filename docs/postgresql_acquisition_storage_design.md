# PostgreSQL 采集数据异步读写设计

实验流程的错误分类、清理恢复和界面状态规则见
[`test_execution_error_recovery_design.md`](test_execution_error_recovery_design.md)。

## 1. 已确认约束

- 使用 PostgreSQL，不使用 SQLite。
- `motorModel + specimenId` 组成批次号，不增加新的批次号配置字段。
- 配置中的 `repeatCount` 表示完整实验重复次数。
- 每次重复实验创建一张独立数据表。
- 数据表只保存相对时间和六通道实验数据。
- `block_sequence`、`source_block`、`partial_block` 等采集块信息只用于上位内存校验，不写入数据库。
- PostgreSQL 读写在独立工作线程执行，不阻塞 ACS 通信线程和 UI 线程。
- 每次进入返回零点状态时停止当前采集；回零完成、尾块提交完成后再启动下一次采集。

## 2. 模块边界

### 2.1 `AcquisitionDatabaseService`

数据库服务负责：

- 加载 `database.json`。
- 管理独立 PostgreSQL 工作线程。
- 创建每次重复实验的数据表。
- 异步写入全部有效采样数据。
- 异步读取实验表列表。
- 按页异步读取实验数据。
- 发布连接、建表、收尾结果和致命数据库故障。

`QSqlDatabase` 连接只在数据库工作线程中创建、使用和销毁。

### 2.2 `TestExecutionService`

测试流程服务协调：

- `MotionControlService`：整组重复运动只启动一次。
- `DataAcquisitionService`：每次重复分别启动、停止和排空。
- `AcquisitionDatabaseService`：每次重复分别建表、写入和结束。

工作台只提交开始或停止意图，不直接操作数据库连接、采集 Buffer 或运动循环边界。

## 3. PostgreSQL 配置

配置文件为程序目录下的 `database.json`：

```json
{
    "schemaVersion": 2,
    "host": "127.0.0.1",
    "port": 5432,
    "databaseName": "eddy_current_bench",
    "userName": "postgres",
    "password": "123",
    "schemaName": "public",
    "connectTimeoutSeconds": 5
}
```

当前按现场要求使用默认超级用户 `postgres`，并在配置文件中明文保存密码 `123`。该文件进入版本控制或安装包后，任何能够读取文件的用户都可以获得整个 PostgreSQL 实例的超级用户权限；生产部署前应改为只拥有实验 schema 权限的专用账号。

当前配置不会自动创建数据库、用户或 schema；这些对象必须在运行验证前由数据库管理员准备。

## 4. 数据表命名

表名由以下部分组成：

```text
<motorModel>_<specimenId>_<yyyyMMdd_HHmmss_zzz>_r<实验次数>
```

例如：

```text
hit_hsm_02_hit_ec_017_20260722_143025_126_r001
```

处理规则：

- 转为小写。
- 非英文字母和数字替换为下划线。
- 重复实验序号使用三位十进制编号。
- 表名超过 PostgreSQL 63 字节限制时截断批次部分并增加 8 位哈希。
- 不使用 `IF NOT EXISTS`，同名表视为错误，防止覆盖历史数据。

## 5. 数据表结构

每张实验表严格只有七列：

```sql
CREATE TABLE <table_name> (
    relative_time_ms  DOUBLE PRECISION PRIMARY KEY,
    acceleration_m_s2 DOUBLE PRECISION NOT NULL,
    velocity_m_s      DOUBLE PRECISION NOT NULL,
    motor_current     DOUBLE PRECISION NOT NULL,
    motor_temperature DOUBLE PRECISION NOT NULL,
    force_n           DOUBLE PRECISION NOT NULL,
    position_m        DOUBLE PRECISION NOT NULL
);
```

不创建固定运行索引表，不在实验表中添加采集块字段或其他元数据字段。异步读取表列表时查询 PostgreSQL `information_schema.tables`。

## 6. 相对时间

每次重复实验的第一点从 `0 ms` 开始：

```text
relative_time_ms = repetitionSampleIndex
                   * samplePeriodSeconds
                   * 1000
```

等价关系为：

```text
samplePeriodMilliseconds = 1000 / sampleFrequencyHz
relative_time_ms = repetitionSampleIndex * samplePeriodMilliseconds
```

当前采集周期为 `0.001 s`，即 `1 ms`、`1000 Hz`。因此相对时间依次为 `0、1、2、3... ms`。时间不使用数据到达上位机或数据库的系统时间，数据库写入延迟不会改变实验时间轴。

## 7. 异步写入

1. `DataAcquisitionService` 完成块级前后校验后发布 `AcquisitionBlock`。
2. `TestExecutionService` 将完整块排队交给数据库服务。
3. 数据库线程校验六个通道长度和采样周期。
4. 每 500 行生成一条参数化多值 `INSERT`。
5. 一个采集块在一个 PostgreSQL 事务内写入。
6. 事务成功后更新本次实验的内存采样偏移。
7. 最后部分块与完整块使用同一写入路径，只写有效采样点。

上位服务不再另外维护待写 Buffer、积压计数或建表/收尾 pending 状态。建表、数据块和收尾命令由数据库工作线程的事件队列保持顺序；只有连接中断、数据完整性异常或 SQL 事务失败才进入统一致命故障出口。

## 8. 异步读取

数据库服务提供两类异步读取：

- 读取当前 schema 下的数据表名称列表。
- 按 `offset + limit` 读取指定表，并按 `relative_time_ms` 排序。

单次分页最多读取 100000 行，避免一次将完整大表装入 UI 内存。

## 9. 重复实验时序

### 9.1 第一次重复

1. 创建第 1 次实验数据表。
2. 写 `DCSTART_CON = 1`。
3. 写入运动参数和 `G_START_REQ = 1`。

### 9.2 每次返回零点

1. 上位轮询到 `G_STATE = 60`。
2. 写 `DCSTART_CON = 0`。
3. 采集 Buffer 停止当前 DC 并发布最后有效部分块。
4. 上位读取尾块并排队写入 PostgreSQL。
5. 轴返回零点后，下位增加 `G_CURRENT_COUNT`。
6. 数据库线程提交尾块并结束当前实验表。

只有步骤 5 和步骤 6 都完成，才允许准备下一次重复。

### 9.3 下一次重复

1. 创建下一次实验数据表。
2. 写 `DCSTART_CON = 1`。
3. 运动 Buffer 从状态 60 的循环边界继续，进入下一次状态 40。

## 10. ACSPL 循环边界

回零完成并更新 `G_CURRENT_COUNT` 后，如果还有下一次重复，运动 Buffer 等待 `DCSTART_CON` 重新置位：

```acspl
IF L_LOOP_INDEX < L_REPEAT_COUNT

    WHILE ^DCSTART_CON & ^G_ABORT_LATCH
        WAIT 1
    END

    IF G_ABORT_LATCH <> 0
        GOTO HANDLE_ABORT
    END

END
```

等待期间轴已经回零，`G_STATE` 保持 60。没有新增 ACSPL 全局变量。

## 11. PostgreSQL 运行依赖

已确认客户端来源：

```text
D:\Tool\postgresql\bin
PostgreSQL 18.4 x64
```

目标程序随配置复制：

- 与 Debug/Release 配置匹配的 Qt 6.5.3 QPSQL 插件。
- `libpq.dll`。
- `libssl-3-x64.dll`。
- `libcrypto-3-x64.dll`。
- `libintl-9.dll`。
- `libiconv-2.dll`。
- `libwinpthread-1.dll`。

安装规则同时保留 PostgreSQL 和命令行工具第三方许可证文件。没有复制 PostgreSQL 服务端程序、数据目录、管理工具或其他无关 DLL。

## 12. 当前验证状态

- 已完成源码、CMake、配置和 ACSPL 循环边界修改。
- `vs2022-x64-debug` 已编译通过。
- 尚未连接 PostgreSQL。
- 尚未创建、写入或读取任何真实数据表。
- 尚未启动上位机、ACS 模拟器或真实控制器。
- PostgreSQL 连接、权限、表名、事务性能以及多次循环时序必须在单独确认 Stage 2 后验证。
