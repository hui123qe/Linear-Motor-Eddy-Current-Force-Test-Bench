#/ Controller version = 4.20
#/ Date = 7/20/2026 1:01 PM
#/ User remarks = 
#0
!PNAME=
!PDESC=
!======================================================================
! Buffer 10：槽位测试主运动程序
!
! 业务流程：
!
!   零点
!      ↓
!   槽位起点 G_START_POS
!      ↓
!   测试终点 G_END_POS
!      ↓
!   返回零点 G_ZERO_POS
!      ↓
!   重复 G_REPEAT_COUNT 次
!
! 使用方法：
!
!   1. 上位机写入所有参数
!   2. 上位机将 G_START_REQ 设置为 1
!   3. Buffer 自动执行测试
!   4. 上位机读取 G_STATE、G_CURRENT_COUNT、G_ERROR_CODE
!
!======================================================================


!======================================================================
! 五、局部变量
!
! 局部变量用于保存本次实验参数快照。
!
! 这样做的目的：
!
!   实验启动以后，即使上位机误修改了 G_START_POS 等变量，
!   当前实验仍然使用启动时保存的参数，不会在运行过程中突然改变。
!============================
REAL L_START_POS
REAL L_END_POS
REAL L_TEST_VEL
INT  L_REPEAT_COUNT

REAL L_ZERO_POS

REAL L_POSITION_VEL
REAL L_POSITION_ACC
REAL L_POSITION_DEC
REAL L_POSITION_JERK

REAL L_TEST_ACC
REAL L_TEST_DEC
REAL L_TEST_JERK

REAL L_MIN_POS
REAL L_MAX_POS

INT L_LOOP_INDEX


!======================================================================
! 六、程序初始化
!======================================================================

! 刚启动 Buffer 时，进入等待状态。
G_STATE = 0

! 清除历史错误。
G_ERROR_CODE = 0

! 清除历史循环次数。
G_CURRENT_COUNT = 0

! 清除停止锁存。
G_ABORT_LATCH = 0

! 清除历史命令。
G_START_REQ = 0
G_STOP_REQ = 0
G_KILL_REQ = 0

!======================================================================
! 七、等待上位机启动
!======================================================================

WAIT_START:

! 等待上位机将 G_START_REQ 写为非零。
!
! TILL 会让当前 Buffer 停在这里，
! 但不会阻塞其他 Buffer。
TILL G_START_REQ <> 0


! 收到启动请求后立即清零。
!
! 这样上位机不需要再次主动清除启动命令，
! 同时可以避免程序结束后因为 G_START_REQ 仍为 1 而再次启动。
G_START_REQ = 0


! 每次新实验开始时清除运行状态。
G_ERROR_CODE = 0
G_CURRENT_COUNT = 0
G_ABORT_LATCH = 0

G_STOP_REQ = 0
G_KILL_REQ = 0


! 进入参数检查状态。
G_STATE = 10


!======================================================================
! 八、复制本次实验参数
!======================================================================

!---------------------- 业务参数快照 ----------------------------

L_START_POS = G_START_POS
L_END_POS = G_END_POS

! 速度只使用绝对值。
!
! 运动方向由目标位置和当前位置决定，
! 因此速度参数不需要携带正负方向。
L_TEST_VEL = ABS(G_TEST_VEL)

L_REPEAT_COUNT = G_REPEAT_COUNT


!---------------------- 设备参数快照 ----------------------------

L_ZERO_POS = G_ZERO_POS

L_POSITION_VEL = ABS(G_N_VEL)
L_POSITION_ACC = ABS(G_N_ACC)
L_POSITION_DEC = ABS(G_N_DEC)
L_POSITION_JERK = ABS(G_N_JERK)

L_TEST_ACC = ABS(G_TEST_ACC)
L_TEST_DEC = ABS(G_TEST_DEC)
L_TEST_JERK = ABS(G_TEST_JERK)





!======================================================================
! 九、参数合法性检查
!======================================================================

IF L_ZERO_POS > L_MAX_POS
    G_ERROR_CODE = 1010
    GOTO PARAM_ERROR
END



!======================================================================
! 十、使能运动轴
!======================================================================

G_STATE = 20


! 使能 X 轴。
!
! 实际项目启动前还应当判断：
!
!   1. 急停是否释放
!   2. 安全门是否关闭
!   3. 光幕是否未触发
!   4. 驱动器是否没有报警
!   5. 轴是否已经完成回零
!   6. 正负限位是否正常
!
ENABLE X


!======================================================================
! 十一、首先运动到零点位置
!======================================================================

G_STATE = 30


! 设置普通定位运动参数。
!
! VEL：目标速度
! ACC：加速度
! DEC：减速度
! JERK：加速度变化率
VEL(X) = L_POSITION_VEL
ACC(X) = L_POSITION_ACC
DEC(X) = L_POSITION_DEC
JERK(X) = L_POSITION_JERK


! 绝对位置运动到零点。
!
! PTP：
!   点到点位置运动。
!
! /e：
!   当前 Buffer 等待运动结束以后，
!   才继续执行下一条程序。
!
PTP/e X, L_ZERO_POS


! 如果运动过程中 Buffer 11 收到了停止请求，
! 运动结束或者停止后会执行到这里。
IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END



!======================================================================
! 十二、初始化循环变量
!======================================================================

L_LOOP_INDEX = 0



!======================================================================
! 十三、重复执行测试
!======================================================================

TEST_LOOP:


! 如果循环次数已经达到目标值，则实验完成。
IF L_LOOP_INDEX >= L_REPEAT_COUNT
    GOTO TEST_FINISHED
END


! 在每次运动之前再次检查停止标志。
IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END



!======================================================================
! 步骤 1：零点运动到槽位起点
!======================================================================

G_STATE = 40


! 使用普通定位参数。
VEL(X) = L_POSITION_VEL
ACC(X) = L_POSITION_ACC
DEC(X) = L_POSITION_DEC
JERK(X) = L_POSITION_JERK


! 从当前位置运动开始位置起点。
PTP/e X, L_START_POS

!======================================================================
! 步骤 2：槽位起点运动到测试终点
!======================================================================

G_STATE = 50


! 切换为测试段运动参数。
!
! ACS 控制器会根据以下参数自动生成运动曲线：
!
!   加速 -> 匀速 -> 减速
!
VEL(X) = L_TEST_VEL
ACC(X) = L_TEST_ACC
DEC(X) = L_TEST_DEC
JERK(X) = L_TEST_JERK


! 执行正式测试运动。
PTP/e X, L_END_POS


!======================================================================
! 步骤 3：测试终点返回零点
!======================================================================

G_STATE = 60


! 返回运动使用返回速度，
! 但仍然使用普通定位加减速度和 Jerk。
VEL(X) = L_POSITION_VEL
ACC(X) = L_POSITION_ACC
DEC(X) = L_POSITION_DEC
JERK(X) = L_POSITION_JERK


! 返回机械零点位置。
PTP/e X, L_ZERO_POS


! 返回过程中可能收到停止请求。
IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END



!======================================================================
! 步骤 4：本次完整循环完成
!======================================================================

! 只有完整执行完：
!
!   零点 -> 起点 -> 终点 -> 零点
!
! 才增加循环次数。
L_LOOP_INDEX = L_LOOP_INDEX + 1


! 将完成次数写给上位机。
G_CURRENT_COUNT = L_LOOP_INDEX


! 继续下一次循环。
GOTO TEST_LOOP



!======================================================================
! 十四、实验正常完成
!======================================================================

TEST_FINISHED:

! 最终完成次数写回全局变量。
G_CURRENT_COUNT = L_LOOP_INDEX

! 设置正常完成状态。
G_STATE = 100

! 正常完成时错误码为 0。
G_ERROR_CODE = 0


! 返回等待状态前，可以在这里等待上位机读取完成状态。
!
! 这里等待 100 个程序周期，避免状态 100 一闪而过。
WAIT 100


! 回到等待启动状态。
G_STATE = 0

GOTO WAIT_START



!======================================================================
! 十五、参数错误处理
!======================================================================

PARAM_ERROR:

! 所有参数错误统一使用状态 -1。
!
! 具体错误原因由 G_ERROR_CODE 区分。
G_STATE = -1


! 保持错误状态一段时间，让上位机能够读取。
WAIT 100


! 参数错误不会使能轴，也不会执行运动。
G_STATE = 0

GOTO WAIT_START



!======================================================================
! 十六、停止处理
!======================================================================

HANDLE_ABORT:


! G_ABORT_LATCH = 1：
! Buffer 11 执行了 HALT X，属于正常减速停止。
IF G_ABORT_LATCH = 1

    G_STATE = -2
    G_ERROR_CODE = 2001

END


! G_ABORT_LATCH = 2：
! Buffer 11 执行了 KILL X，属于快速停止。
IF G_ABORT_LATCH = 2

    G_STATE = -3
    G_ERROR_CODE = 2002

END


! 停止后保持当前所在位置。
!
! 不在这里自动返回零点，因为：
!
!   1. 急停或安全联锁后，自动运动可能不安全。
!   2. 停止时不知道机械周围是否有人。
!   3. 应由上位机确认安全后再发复位或回零命令。


! 等待上位机读取停止状态。
WAIT 100


! 清除停止锁存。
G_ABORT_LATCH = 0

! 回到等待启动状态。
G_STATE = 0

GOTO WAIT_START
#1
!PNAME=
!PDESC=
!============================================================
! Persistent block data collection service
! Start Buffer #2 only once.
! Use DCSTART_CON to start or stop collection repeatedly.
!============================================================


DCCOUNT = 0
DCSTART_CON = 0
DCSTART = 0

DC_ACTIVE_BLOCK = 0
DC_FINISHED_BLOCK = 0
DC_BLOCK_SEQUENCE = 0


! Buffer stays alive permanently
WHILE 1

    !--------------------------------------------------------
    ! Wait until the host requests data collection
    !--------------------------------------------------------

    TILL DCSTART_CON = 1

    ! Reset collection session state
    DCCOUNT = 0
    DCSTART = 1
    DC_ACTIVE_BLOCK = 0


    !--------------------------------------------------------
    ! Wait for Axis 2 to start moving
    !--------------------------------------------------------

    IF DCSTART

        ! Wait for motion or a stop request
        WHILE DCSTART_CON & ^AST(0).#MOVE
            WAIT 1
        END

        ! If collection was stopped before motion started,
        ! return to the idle state
        IF ^DCSTART_CON
            DCSTART = 0
        ELSE
            DCSTART = 0
        END

    END


    !--------------------------------------------------------
    ! Continuous block data collection
    !--------------------------------------------------------

    WHILE DCSTART_CON

        ! Select the next circular data block
        DCCOUNT = DCCOUNT + 1

        IF DCCOUNT > 5
            DCCOUNT = 1
        END

        ! Notify the host which block is being written
        DC_ACTIVE_BLOCK = DCCOUNT


        ! Start data collection into the selected block
        IF DCCOUNT = 1

            DC DC_Data_1, ARRAYCOUNT, 1, FACC(2), FVEL(2), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(2)

        ELSEIF DCCOUNT = 2

            DC DC_Data_2, ARRAYCOUNT, 1, FACC(2), FVEL(2), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(2)

        ELSEIF DCCOUNT = 3

            DC DC_Data_3, ARRAYCOUNT, 1, FACC(2), FVEL(2), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(2)

        ELSEIF DCCOUNT = 4

            DC DC_Data_4, ARRAYCOUNT, 1,FACC(2), FVEL(2), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(2)

        ELSEIF DCCOUNT = 5

            DC DC_Data_5, ARRAYCOUNT, 1, FACC(2), FVEL(2), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(2)

        END


        ! Wait until collection finishes or is stopped
        TILL ^S_ST.#DC


        ! Only publish a completely collected block
        IF DCSTART_CON

            DC_FINISHED_BLOCK = DCCOUNT
            DC_BLOCK_SEQUENCE = DC_BLOCK_SEQUENCE + 1

        END

        DC_ACTIVE_BLOCK = 0

    END


    !--------------------------------------------------------
    ! Collection stopped; return to idle state
    !--------------------------------------------------------

    DC_ACTIVE_BLOCK = 0
    DCSTART = 0

END

STOP


!============================================================
! Stop current DC collection when DCSTART_CON changes 1 -> 0
!============================================================

ON ^DCSTART_CON

BLOCK

    ! Stop an active system data collection
    IF S_ST.#DC
        STOPDC
    END

    ! Notify the host that no block is being written
    DC_ACTIVE_BLOCK = 0

END

RET
#A
!PNAME=
!PDESC=
!======================================================================
! 一、上位机写入的业务参数
!======================================================================
GLOBAL AXISDEF X = 0
! 槽位起始位置
GLOBAL INT G_ABORT_LATCH
! 表示正式测试运动开始的位置。
! 例如槽位位于 100 mm，则设置为 100。
GLOBAL REAL G_START_POS =50


! 测试结束位置
!
! 电机从 G_START_POS 运动到 G_END_POS。
! 该运动过程中完成：
!
!   加速 -> 匀速 -> 减速
!
GLOBAL REAL G_END_POS =1000


! 测试段目标速度
!
! 注意：
! 这是目标速度，并不保证一定存在匀速段。
! 如果起点和终点距离太短，电机可能还没有达到该速度，
! 就已经需要开始减速。
GLOBAL REAL G_TEST_VEL=500


! 重复次数
!
! 一次完整循环为：
!
!   零点 -> 起点 -> 终点 -> 零点
!
GLOBAL INT G_REPEAT_COUNT =10



!======================================================================
! 二、设备配置参数
!
! 这些参数通常不是每次实验都让操作员输入，
! 而是由设备配置文件或者调试页面设置。
!======================================================================

! 机械零点坐标
!
! 如果机械坐标已经正确建立，通常设置为 0。
GLOBAL REAL G_ZERO_POS=0



! 普通定位速度
GLOBAL REAL G_N_VEL = 1000
GLOBAL REAL G_N_ACC = 1000
GLOBAL REAL G_N_DEC = 1000

! 普通定位运动 Jerk
!
! Jerk 表示加速度变化率。
! Jerk 越小，运动越平滑；
! Jerk 太大，机械冲击可能更明显。
GLOBAL REAL G_N_JERK  = 10000
! 测试运动加速度
!
! 用于：
!
!   G_START_POS -> G_END_POS
!
GLOBAL REAL G_TEST_ACC = 1000

! 测试运动减速度
GLOBAL REAL G_TEST_DEC = 1000

! 测试运动 Jerk
GLOBAL REAL G_TEST_JERK =10000



!======================================================================
! 三、上位机命令变量
!======================================================================

! 启动请求
!
! 上位机写：
!
!   G_START_REQ = 1
!
! Buffer 接收到请求后会自动将其恢复为 0。
GLOBAL INT G_START_REQ

! 正常停止请求
!
! 该变量由 Buffer 11 处理。
GLOBAL INT G_STOP_REQ

! 快速停止请求
!
! 该变量由 Buffer 11 处理。
GLOBAL INT G_KILL_REQ



!======================================================================
! 四、程序运行状态
!======================================================================

! 当前程序状态
!
! 状态定义：
!
!    0    等待启动
!   10    参数检查
!   20    使能轴
!   40    正在运动到槽位起点
!   50    正在执行测试运动
!   60    正在返回零点
!  100    实验正常完成
!
!   -1    参数错误
!   -2    正常停止
!   -3    快速停止
!   -4    程序异常
!
GLOBAL INT G_STATE


! 错误码
!
!    0    无错误
!
! 1001    重复次数非法
! 1002    起点和终点相同
! 1003    测试速度非法
! 1004    定位速度非法
! 2001    收到正常停止请求
! 2002    收到快速停止请求
!
GLOBAL INT G_ERROR_CODE


! 已完成循环次数
!
! 只有在：
!
!   起点 -> 终点 -> 零点
!
! 全部完成后，才增加一次。
GLOBAL INT G_CURRENT_COUNT


! 停止锁存状态
!
!   0：没有停止请求
!   1：正常停止
!   2：快速停止
!
! 该变量由 Buffer 11 设置。
GLOBAL INT G_ABORT_LATCHg



! Current circular buffer index
GLOBAL INT DCCOUNT

! Master data collection control flag
GLOBAL INT DCSTART_CON

! First-start motion waiting flag
GLOBAL INT DCSTART

! Number of the block currently being written
GLOBAL INT DC_ACTIVE_BLOCK

! Number of the latest completed block
GLOBAL INT DC_FINISHED_BLOCK

! Sequence number of completed data blocks
GLOBAL INT DC_BLOCK_SEQUENCE




! Calculated force value
GLOBAL REAL CURRFORCE
! Motor current value in amperes
GLOBAL REAL MOTOR_CURRENT

! Motor temperature value in degrees Celsius
GLOBAL REAL MOTOR_TEMPERATURE
!============================================================
! Data collection arrays
!
! Channel 0: Feedback acceleration
! Channel 1: Feedback velocity
! Channel 2: Motor current
! Channel 3: Motor temperature
! Channel 4: Current force
! Channel 5: Feedback position
!============================================================

GLOBAL INT ARRAYCOUNT = 16666

GLOBAL REAL DC_Data_1(6)(16666)
GLOBAL REAL DC_Data_2(6)(16666)
GLOBAL REAL DC_Data_3(6)(16666)
GLOBAL REAL DC_Data_4(6)(16666)
GLOBAL REAL DC_Data_5(6)(16666)
