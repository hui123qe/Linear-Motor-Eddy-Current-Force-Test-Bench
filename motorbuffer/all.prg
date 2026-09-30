#/ Controller version = 4.20
#/ Date = 9/30/2026 12:55 PM
#/ User remarks = 
#1
!PNAME=
!PDESC=
AUTOEXEC:

TILL ECST.#OP = 1


EC_DOUT3.4=0!力传感器去皮
ECOUT(208,EC_DOUT3)



while 1
! 数字量输出
ECOUT(226,EC_DOUT1)
ECOUT(227,EC_DOUT2)
EC_DOUT1.0=Value_OUT!总进气电磁阀
EC_DOUT1.1=NC_OUT1!预留输出1
EC_DOUT1.2=NC_OUT2!预留输出2
EC_DOUT1.3=NC_OUT3!预留输出3
EC_DOUT1.4=NC_OUT4!预留输出4
EC_DOUT1.5=NC_OUT5!预留输出5
EC_DOUT1.6=NC_OUT6!预留输出6
EC_DOUT1.7=NC_OUT7!预留输出7
EC_DOUT2.0=NC_OUT8!预留输出8
EC_DOUT2.1=NC_OUT9!预留输出9
EC_DOUT2.2=NC_OUT10!预留输出10
EC_DOUT2.3=NC_OUT11!预留输出11
EC_DOUT2.4=NC_OUT12!预留输出12
EC_DOUT2.5=NC_OUT13!预留输出13
EC_DOUT2.6=NC_OUT14!预留输出14
EC_DOUT2.7=NC_OUT15!预留输出15
Value_OUT=1

! 数字量输入
ECIN(288,EC_DIN1)
ECIN(289,EC_DIN2)
Main_Pressure=EC_DIN1.0!总进气压力传感器
Safetylight1=EC_DIN1.1!光栅1
Safetylight2=EC_DIN1.2!光栅2
Safetylight3=EC_DIN1.3!光栅3
Safetylight4=EC_DIN1.4!光栅4
Up1_Flow=EC_DIN1.5!顶部流量计1
Up2_Flow=EC_DIN1.6!顶部流量计2
Side1_Flow=EC_DIN1.7!侧边流量计1
Side2_Flow=EC_DIN2.0!侧边流量计2
Down_Flow=EC_DIN2.1!底部流量计
Value_IN=EC_DIN2.2!电磁阀反馈
NC_DIN1=EC_DIN2.3!预留输入1 
NC_DIN2=EC_DIN2.4!预留输入2
NC_DIN3=EC_DIN2.5!预留输入3
NC_DIN4=EC_DIN2.6!预留输入4
NC_DIN5=EC_DIN2.7!预留输入5

!模拟量输入
ECIN(242,EC_AIN1)
ECIN(248,EC_AIN2)
ECIN(254,EC_AIN3)
ECIN(260,EC_AIN4)! 预留
ECIN(266,EC_AIN5)
ECIN(272,EC_AIN6)! 预留
ECIN(278,EC_AIN7)! 预留
ECIN(284,EC_AIN8)! 预留

Up1_FLowA=EC_AIN1*2.5!顶部流量计1，单位L/min
Up2_FLowA=EC_AIN2*2.5!顶部流量计2，单位L/min
Side1_FLowA=EC_AIN3*2.5!侧边流量计1，单位L/min
Side2_FLowA=EC_AIN4*2.5!侧边流量计1，单位L/min
Down_FLowA=EC_AIN5*2.5!底部流量计1，单位L/min
!力传感器输入
ECIN(210,EC_AIN9)
Froce_Sensor=EC_AIN9

end
STOP
#2
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
!      ↓ 正向测试
!   测试终点 G_END_POS
!      ↓ 结果处理等待
!      ↓ 反向镜像测试
!   槽位起点 G_START_POS
!      ↓ 结果处理等待
!   重复 G_REPEAT_COUNT 次，最后返回零点
!
! 使用方法：
!
!   1. 上位机写入所有参数
!   2. 上位机将 G_START_REQ 设置为 1
!   3. Buffer 自动执行测试
!   4. 上位机读取 G_STATE、G_CURRENT_COUNT、G_ERROR_CODE
!
!======================================================================
AUTOEXEC:


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
REAL L_RESULT_WAIT

REAL L_MIN_POS
REAL L_MAX_POS

INT L_LOOP_INDEX
INT L_RECORD_INDEX


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
L_RESULT_WAIT = G_RESULT_WAIT





!======================================================================
! 九、参数合法性检查
!======================================================================

IF L_ZERO_POS > L_MAX_POS
    G_ERROR_CODE = 1010
    GOTO PARAM_ERROR
END

IF L_RESULT_WAIT <= 0
    G_ERROR_CODE = 1005
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


! 启动 Buffer 去皮
START 8, 1
TILL (PST(8).#RUN = 0)


! 如果运动过程中 Buffer 11 收到了停止请求，
! 运动结束或者停止后会执行到这里。
IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END



!======================================================================
! 十二、运动到槽位起点
!======================================================================

G_STATE = 40


! 使用普通定位参数。
VEL(X) = L_POSITION_VEL
ACC(X) = L_POSITION_ACC
DEC(X) = L_POSITION_DEC
JERK(X) = L_POSITION_JERK


! 从当前位置运动开始位置起点。
PTP/e X, L_START_POS

IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END

!======================================================================
! 十三、初始化循环变量并重复执行正反向测试
!======================================================================

L_LOOP_INDEX = 0
L_RECORD_INDEX = 0

TEST_LOOP:

IF L_LOOP_INDEX >= L_REPEAT_COUNT
    GOTO TEST_FINISHED
END

IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END

!======================================================================
! 步骤 1：槽位起点正向运动到测试终点
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

IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END

!======================================================================
! 步骤 2：等待上位机完成正向记录处理
!======================================================================

L_RECORD_INDEX = L_RECORD_INDEX + 1
G_CURRENT_COUNT = L_RECORD_INDEX
G_STATE = 55

WAIT L_RESULT_WAIT

IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END

!======================================================================
! 步骤 3：从正向停止位置执行反向镜像测试
!======================================================================

G_STATE = 70

VEL(X) = L_TEST_VEL
ACC(X) = L_TEST_ACC
DEC(X) = L_TEST_DEC
JERK(X) = L_TEST_JERK

PTP/e X, L_START_POS

IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END

!======================================================================
! 步骤 4：等待上位机完成反向记录处理
!======================================================================

L_RECORD_INDEX = L_RECORD_INDEX + 1
G_CURRENT_COUNT = L_RECORD_INDEX
G_STATE = 75

WAIT L_RESULT_WAIT

IF G_ABORT_LATCH <> 0
    GOTO HANDLE_ABORT
END

! 一次正向和一次反向实验全部完成后，循环次数加一。
L_LOOP_INDEX = L_LOOP_INDEX + 1

GOTO TEST_LOOP



!======================================================================
! 十四、实验正常完成并返回零点
!======================================================================

TEST_FINISHED:

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

! 最终完成记录数写回全局变量，每个循环固定包含正向、反向两条记录。
G_CURRENT_COUNT = L_RECORD_INDEX

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
#3
!PNAME=
!PDESC=
!============================================================
! Persistent block data collection service
! Start Buffer #2 only once.
! Use DCSTART_CON to start or stop collection repeatedly.
!============================================================
AUTOEXEC:

DCCOUNT = 0
DCSTART_CON = 0


DC_ACTIVE_BLOCK = 0
DC_FINISHED_BLOCK = 0
DC_BLOCK_SEQUENCE = 0


! Buffer stays alive permanently
WHILE 1

    !--------------------------------------------------------
    ! Wait until the host requests data collection
    !--------------------------------------------------------

    TILL DCSTART_CON = 1

    !--------------------------------------------------------
    ! Reset the new collection session
    !
    ! The host must finish reading the previous session
    ! before setting DCSTART_CON to 1 again.
    !--------------------------------------------------------

    DCCOUNT = 0

    DC_ACTIVE_BLOCK = 0

    DC_FINISHED_BLOCK = 0
    DC_FINISHED_COUNT = 0
    DC_FINISHED_PARTIAL = 0

    DC_BLOCK_SEQUENCE = 0
    DC_CURRENT_VALID_COUNT = 0


    ! Clear block metadata from the previous session

    DC_BLOCK_VALID_COUNT(0) = 0
    DC_BLOCK_VALID_COUNT(1) = 0
    DC_BLOCK_VALID_COUNT(2) = 0
    DC_BLOCK_VALID_COUNT(3) = 0
    DC_BLOCK_VALID_COUNT(4) = 0

    DC_BLOCK_PARTIAL_MAP(0) = 0
    DC_BLOCK_PARTIAL_MAP(1) = 0
    DC_BLOCK_PARTIAL_MAP(2) = 0
    DC_BLOCK_PARTIAL_MAP(3) = 0
    DC_BLOCK_PARTIAL_MAP(4) = 0

    DC_BLOCK_SEQ_MAP(0) = 0
    DC_BLOCK_SEQ_MAP(1) = 0
    DC_BLOCK_SEQ_MAP(2) = 0
    DC_BLOCK_SEQ_MAP(3) = 0
    DC_BLOCK_SEQ_MAP(4) = 0


    !--------------------------------------------------------
    ! Wait for Axis  to start moving
    !--------------------------------------------------------
    WHILE DCSTART_CON & ^AST(X).#MOVE
        WAIT 1
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


        ! Clear metadata of this block before reuse
        DC_BLOCK_VALID_COUNT(DCCOUNT - 1) = 0
        DC_BLOCK_PARTIAL_MAP(DCCOUNT - 1) = 0


        ! Start data collection into the selected block
        IF DCCOUNT = 1

            DC DC_Data_1, ARRAYCOUNT, 1, FACC(X), FVEL(X), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(X)

        ELSEIF DCCOUNT = 2

            DC DC_Data_2, ARRAYCOUNT, 1, FACC(X), FVEL(X), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(X)

        ELSEIF DCCOUNT = 3

            DC DC_Data_3, ARRAYCOUNT, 1, FACC(X), FVEL(X), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(X)

        ELSEIF DCCOUNT = 4

            DC DC_Data_4, ARRAYCOUNT, 1,FACC(X), FVEL(X), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(X)

        ELSEIF DCCOUNT = 5

            DC DC_Data_5, ARRAYCOUNT, 1, FACC(X), FVEL(X), MOTOR_CURRENT, MOTOR_TEMPERATURE, CURRFORCE, FPOS(X)

        END


        ! Wait until collection finishes or is stopped
        TILL ^S_ST.#DC



        !----------------------------------------------------
        ! Capture S_DCN immediately
        !
        ! Normal completion:
        !     S_DCN = ARRAYCOUNT
        !
        ! STOPDC:
        !     0 <= S_DCN < ARRAYCOUNT
        !----------------------------------------------------


        DC_CURRENT_VALID_COUNT = S_DCN


       IF DC_CURRENT_VALID_COUNT > 0

            ! First write block metadata
            DC_BLOCK_VALID_COUNT(DCCOUNT - 1) =   DC_CURRENT_VALID_COUNT

            IF DC_CURRENT_VALID_COUNT < ARRAYCOUNT

                DC_BLOCK_PARTIAL_MAP(DCCOUNT - 1) = 1

            ELSE

                DC_BLOCK_PARTIAL_MAP(DCCOUNT - 1) = 0

            END


            ! Generate new publication sequence
            DC_BLOCK_SEQUENCE = DC_BLOCK_SEQUENCE + 1

            DC_BLOCK_SEQ_MAP(DCCOUNT - 1) =  DC_BLOCK_SEQUENCE


            ! Publish latest block information
            DC_FINISHED_BLOCK = DCCOUNT

            DC_FINISHED_COUNT =  DC_CURRENT_VALID_COUNT

            DC_FINISHED_PARTIAL =  DC_BLOCK_PARTIAL_MAP(DCCOUNT - 1)

        END


    END


    !--------------------------------------------------------
    ! Collection stopped; return to idle state
    !--------------------------------------------------------

    DC_ACTIVE_BLOCK = 0



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



END

RET
#4
!PNAME=
!PDESC=
!============================================================
! 正弦模拟变量
!
! 公式：
! SIM_VALUE = OFFSET + AMPLITUDE * SIN(2 * PI * FREQ * TIME)
!============================================================

GLOBAL REAL SIM_VALUE
GLOBAL REAL SIM_AMPLITUDE
GLOBAL REAL SIM_OFFSET
GLOBAL REAL SIM_FREQ_HZ
GLOBAL REAL SIM_PHASE_DEG
GLOBAL INT  SIM_RUN

REAL SIM_START_TIME
REAL SIM_TIME_SEC
REAL SIM_ANGLE
REAL SIM_PI


!------------------------------------------------------------
! 参数初始化
!------------------------------------------------------------

SIM_PI = 3.141592653589793

SIM_AMPLITUDE = 10.0    ! 振幅
SIM_OFFSET = 20.0       ! 中心值
SIM_FREQ_HZ = 0.5       ! 频率，单位 Hz
SIM_PHASE_DEG = 0.0     ! 初始相位，单位度

SIM_RUN = 1

! 记录程序启动时间
SIM_START_TIME = TIME


!------------------------------------------------------------
! 持续生成正弦变量
!------------------------------------------------------------

WHILE SIM_RUN

    ! TIME 的单位是毫秒，转换成秒
    SIM_TIME_SEC = (TIME - SIM_START_TIME) / 1000.0

    ! SIN 使用弧度
    SIM_ANGLE = 2.0 * SIM_PI * SIM_FREQ_HZ * SIM_TIME_SEC
    SIM_ANGLE = SIM_ANGLE + SIM_PHASE_DEG * SIM_PI / 180.0

    CURRFORCE = SIM_OFFSET + SIM_AMPLITUDE * SIN(SIM_ANGLE)

    ! 每 10ms 更新一次
    WAIT 0.25

END


! 正常退出时恢复到中心值
SIM_VALUE = SIM_OFFSET

STOP
#5
!PNAME=Y3&Y4 Homing
!PDESC=
INT Axis3

GLOBAL INT Y3_ErrCompensation,Y3_Zone
GLOBAL REAL Y3_Base,Y3_Increment,Y3_Homeoffset

Axis3=2
Y3_ErrCompensation = 0
Y3_Zone =0 !NO CHANGE

DISABLE (2,3)
MFLAGS2.25=0
MFLAGS3.25=0

ERRORUNMAP Axis3, Y3_Zone
WAIT 100
Y3_Homeoffset=-5
FCLEAR ALL
!homing
ACC(Axis3)=500
DEC(Axis3)=500
JERK(Axis3)=2000

!!!!Decrease PID before Servo On !!!
!!SLVKP(2)=100;SLVKI(2)=0;SLPKP(2)=50;SLVSOF(2)=400;SLVSOFD(0)=0.7 
!SLVKP(2)=200;SLVKI(2)=0;SLAFF(2)=50;SLSBORD(2)=0;SLSBBW(2)=80;SLSBF(2)=110;
!XCURV(2)=20; XCURI(2)=10;

ENABLE Axis3
TILL MST(Axis3).#ENABLED

!!!!Recover normal PID After Servo On!!!
!SLVKP(2)=1200;SLVKI(2)=150;SLPKP(2)=50;SLVSOF(2)=400;SLVSOFD(0)=0.7 
!XCURV(2)=100; XCURI(2)=50;

HOME Axis3,17,20,3600,Y3_Homeoffset
TILL MFLAGS(Axis3).#HOME=1
PTP/E Axis3,0

!STOP

Disp "Homing successful!"
DISABLE(2,3)
MFLAGS2.25=1
MFLAGS3.25=1
wait 100
ENABLE (2,3)
PTP/E Axis3,0



if Y3_ErrCompensation = 1
        DISP "Enable the compensation mode."
        DISP "EYecute the compensation prcoess......"
        CALL Y3_ErrorCompensate       ;
else
        DISP "Error compensation disabled!"
end
STOP

Y3_ErrorCompensate:
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!dYnamic error compensation FOR AYis 0 !
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
Y3_Base = -66
Y3_Increment = 20

Y3Y4_CORRECTION_MAP(0)=        0       ;
Y3Y4_CORRECTION_MAP(1)=        0        ;
Y3Y4_CORRECTION_MAP(2)=        0        ;
Y3Y4_CORRECTION_MAP(3)=        0         ;
Y3Y4_CORRECTION_MAP(4)=        0         ;
Y3Y4_CORRECTION_MAP(5)=        0         ;
Y3Y4_CORRECTION_MAP(6)=        0         ;
Y3Y4_CORRECTION_MAP(7)=        0         ;
Y3Y4_CORRECTION_MAP(8)=        0         ;
Y3Y4_CORRECTION_MAP(9)=        0         ;
Y3Y4_CORRECTION_MAP(10)=        0         ;
Y3Y4_CORRECTION_MAP(11)=        0         ;
Y3Y4_CORRECTION_MAP(12)=        0         ;
Y3Y4_CORRECTION_MAP(13)=        0         ;
Y3Y4_CORRECTION_MAP(14)=        0         ;
Y3Y4_CORRECTION_MAP(15)=        0         ;
Y3Y4_CORRECTION_MAP(16)=        0         ;
Y3Y4_CORRECTION_MAP(17)=        0         ;
Y3Y4_CORRECTION_MAP(18)=        0         ;
Y3Y4_CORRECTION_MAP(19)=        0         ;
Y3Y4_CORRECTION_MAP(20)=        0         ;
Y3Y4_CORRECTION_MAP(21)=        0         ;
Y3Y4_CORRECTION_MAP(22)=        0         ;
Y3Y4_CORRECTION_MAP(23)=        0         ;
Y3Y4_CORRECTION_MAP(24)=        0         ;
Y3Y4_CORRECTION_MAP(25)=        0         ;
Y3Y4_CORRECTION_MAP(26)=        0         ;


ERRORUNMAP 0, Y3_Zone
ERRORMAP1D 0, Y3_Zone, Y3_Base, Y3_Increment, Y3Y4_CORRECTION_MAP
ERRORMAPON 0, Y3_Zone

STOP
#6
!PNAME=Y1&Y2 Homing
!PDESC=
INT Axis

GLOBAL INT Y_ErrCompensation,Y_Zone
GLOBAL REAL Y_Base,Y_Increment,Y_Homeoffset

Axis=0
Y_ErrCompensation = 0
Y_Zone =0 !NO CHANGE

ERRORUNMAP Axis, Y_Zone
WAIT 100
Y_Homeoffset=5
FCLEAR ALL
!homing
ACC(Axis)=200
DEC(Axis)=200
JERK(Axis)=2000

!!!!Decrease PID before Servo On !!!
!!SLVKP(2)=100;SLVKI(2)=0;SLPKP(2)=50;SLVSOF(2)=400;SLVSOFD(0)=0.7 
!SLVKP(2)=200;SLVKI(2)=0;SLAFF(2)=50;SLSBORD(2)=0;SLSBBW(2)=80;SLSBF(2)=110;
!XCURV(2)=20; XCURI(2)=10;

ENABLE Axis
TILL MST(Axis).#ENABLED

!!!!Recover normal PID After Servo On!!!
!SLVKP(2)=1200;SLVKI(2)=150;SLPKP(2)=50;SLVSOF(2)=400;SLVSOFD(0)=0.7 
!XCURV(2)=100; XCURI(2)=50;

HOME Axis,18,10,2000,Y_Homeoffset
TILL MFLAGS(Axis).#HOME=1
PTP/E Axis,0
STOP

Disp "Homing successful!"
if Y_ErrCompensation = 1
        DISP "Enable the compensation mode."
        DISP "EYecute the compensation prcoess......"
        CALL Y_ErrorCompensate       ;
else
        DISP "Error compensation disabled!"
end

!STOP

Y_ErrorCompensate:
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!dYnamic error compensation FOR AYis 0 !
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
Y_Base = -66
Y_Increment = 20

Y1Y2_CORRECTION_MAP(0)=        0       ;
Y1Y2_CORRECTION_MAP(1)=        0        ;
Y1Y2_CORRECTION_MAP(2)=        0        ;
Y1Y2_CORRECTION_MAP(3)=        0         ;
Y1Y2_CORRECTION_MAP(4)=        0         ;
Y1Y2_CORRECTION_MAP(5)=        0         ;
Y1Y2_CORRECTION_MAP(6)=        0         ;
Y1Y2_CORRECTION_MAP(7)=        0         ;
Y1Y2_CORRECTION_MAP(8)=        0         ;
Y1Y2_CORRECTION_MAP(9)=        0         ;
Y1Y2_CORRECTION_MAP(10)=        0         ;
Y1Y2_CORRECTION_MAP(11)=        0         ;
Y1Y2_CORRECTION_MAP(12)=        0         ;
Y1Y2_CORRECTION_MAP(13)=        0         ;
Y1Y2_CORRECTION_MAP(14)=        0         ;
Y1Y2_CORRECTION_MAP(15)=        0         ;
Y1Y2_CORRECTION_MAP(16)=        0         ;
Y1Y2_CORRECTION_MAP(17)=        0         ;
Y1Y2_CORRECTION_MAP(18)=        0         ;
Y1Y2_CORRECTION_MAP(19)=        0         ;
Y1Y2_CORRECTION_MAP(20)=        0         ;
Y1Y2_CORRECTION_MAP(21)=        0         ;
Y1Y2_CORRECTION_MAP(22)=        0         ;
Y1Y2_CORRECTION_MAP(23)=        0         ;
Y1Y2_CORRECTION_MAP(24)=        0         ;
Y1Y2_CORRECTION_MAP(25)=        0         ;
Y1Y2_CORRECTION_MAP(26)=        0         ;


ERRORUNMAP 0, Y_Zone
ERRORMAP1D 0, Y_Zone, Y_Base, Y_Increment, Y1Y2_CORRECTION_MAP
ERRORMAPON 0, Y_Zone

STOP
#7
!PNAME=Home
!PDESC=
! ==========================================
! Buffer ：总回零
! 正常运行此Buffer，从第一行开始
! ==========================================
GLOBAL INT HomeDone
GLOBAL INT HomeRunning



! 防止重复启动
IF PST(5).#RUN | PST(6).#RUN
    DISP "Homing buffer is already running!"
    STOP
END


! ---------- 开始回零 ----------


HomeDone = 0
HomeRunning = 1

! ---------- 连续启动，两组并发执行 ----------
DISP "Start both homing buffers."

START 5, 1
START 6, 1


! ---------- 等待两个程序都结束 ----------
TILL (PST(5).#RUN = 0) & (PST(6).#RUN = 0)

! ---------- 全部成功 ----------
HomeDone = 1
HomeRunning = 0

STOP



! ---------- 上电初始化 ----------
AUTOEXEC:

HomeDone = 0
HomeRunning = 0


STOP
#8
!PNAME=Tare
!PDESC=


! 前提：输出映射已建立，去皮位当前为0
IF ECST.#OP = 0
    DISP "EtherCAT is not operational!"
    STOP
END

! 发出去皮信号
EC_DOUT3.4 = 1

! 示例保持100ms，实际按传感器手册设置
WAIT 100

! 释放去皮信号，为下一次触发做准备
EC_DOUT3.4 = 0

STOP
#A
!PNAME=
!PDESC=
global int EC_DOUT1,EC_DOUT2,EC_DIN1,EC_DIN2,EC_DOUT3
global real EC_AIN1,EC_AIN2,EC_AIN3,EC_AIN4,EC_AIN5,EC_AIN6,EC_AIN7,EC_AIN8,EC_AIN9
global int Value_OUT,NC_OUT1,NC_OUT2,NC_OUT3,NC_OUT4,NC_OUT5,NC_OUT6,NC_OUT7,NC_OUT8,NC_OUT9,NC_OUT10,NC_OUT11,NC_OUT12,NC_OUT13,NC_OUT14,NC_OUT15! 数字量输出变量定义
global int Main_Pressure,Safetylight1,Safetylight2,Safetylight3,Safetylight4,Up1_Flow,Up2_Flow,Side1_Flow,Side2_Flow,Down_Flow,Value_IN,NC_DIN1,NC_DIN2,NC_DIN3,NC_DIN4,NC_DIN5! 数字量输入变量定义
global real Up1_FLowA,Up2_FLowA,Side1_FLowA,Side2_FLowA,Down_FLowA,NC_AIN1,NC_AIN2,NC_AIN3! 模拟量输入变量定义
global int Tara!去皮
global real Froce_Sensor!力传感器读值



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
!   起点 -> 终点（正向）-> 起点（反向）
!
GLOBAL INT G_REPEAT_COUNT =10

! 每段正向或反向实验结束后的结果处理等待时间。
! 当前控制器程序周期按 40 ms 计算，125 个周期约为 5 秒。
GLOBAL REAL G_RESULT_WAIT = 125



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
!   50    正在执行正向测试运动
!   55    正向结果处理等待
!   60    全部循环完成后正在返回零点
!   70    正在执行反向测试运动
!   75    反向结果处理等待
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


! 已完成实验记录数
!
! 每完成一次正向或反向测试增加一次，因此最终值为重复次数的两倍。
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



GLOBAL INT DC_FINISHED_COUNT
GLOBAL INT DC_FINISHED_PARTIAL
GLOBAL INT DC_CURRENT_VALID_COUNT

GLOBAL INT DC_BLOCK_VALID_COUNT(5)
GLOBAL INT DC_BLOCK_SEQ_MAP(5)
GLOBAL INT DC_BLOCK_PARTIAL_MAP(5)

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!         回零+补偿变量定义          !!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
GLOBAL REAL STATIC Y1Y2_CORRECTION_MAP (47)
GLOBAL REAL STATIC Y3Y4_CORRECTION_MAP (47)
!GLOBAL REAL X_Acc,Y_Acc,Z_Acc
