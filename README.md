# 麦克纳姆小车调试项目

本项目对应当前照片和接线：经典 ESP32、板载四路 SA8345、四路单信号脉冲输入。使用 PlatformIO + Arduino，通过 USB 串口完成轮位检查、方向校准、编码器检查和 X 型麦克纳姆底盘运动测试。

上电保持停止；每条运动指令默认运行 1000 ms，最长 5000 ms，到时自动停止。程序没有无限运行模式。实际转向、轮子安装旋向和编码器规格需要通过实车调试确认。

## 1. 引脚约定

左右以面向车头的方向判断。下表按你描述的顺序，以及两张照片中插口从上到下的顺序建立。照片无法追踪线束全程，首次先用 `scan` 核对轮位。

| 顺序 | 轮位 | 命令名 | 电机 INA | 电机 INB | 编码器/脉冲信号 |
| --- | --- | --- | --- | --- | --- |
| 1 | 左前 | `fl` | GPIO13 | GPIO15 | GPIO32 |
| 2 | 右前 | `fr` | GPIO12 | GPIO14 | GPIO34 |
| 3 | 左后 | `rl` | GPIO27 | GPIO26 | GPIO35 |
| 4 | 右后 | `rr` | GPIO25 | GPIO33 | GPIO36 |

电机插口旁的两个 GPIO 编号指向板载驱动器的控制输入；程序通过驱动器给电机供电。三针接口按外壳标识连接 GND、3.3V、信号。

如实测轮位与表格不同，修改 `include/board_config.h` 中的 `kWheels`。这比在多处交换电机编号更容易检查。

## 2. 编译、烧录与打开串口

可直接用 VS Code 的 PlatformIO 扩展打开本文件夹。也可以在 PowerShell 中运行下面的命令。辅助脚本会在本项目的 `.tools/venv` 创建独立 Python 环境并安装 PlatformIO；首次编译需要联网下载 ESP32 工具链。

```powershell
cd 'D:\Codex\麦克小车\mecanum-debug'

# 编译
powershell -ExecutionPolicy Bypass -File .\tools\project.ps1 -Action build

# 查找串口；USB 数据线接好后应看到 COM 端口
powershell -ExecutionPolicy Bypass -File .\tools\project.ps1 -Action ports

# 将 COM5 换成实际端口。烧录前架空车轮。
powershell -ExecutionPolicy Bypass -File .\tools\project.ps1 -Action upload -Port COM5

# 打开串口命令终端，波特率 115200，按回车发送
powershell -ExecutionPolicy Bypass -File .\tools\project.ps1 -Action monitor -Port COM5
```

串口终端按 `Ctrl+C` 退出。重新烧录前先关闭终端，避免占用端口。若终端打开后没有看到开机信息，输入 `status` 并回车。

默认配置固定为 PlatformIO `espressif32@6.12.0`、Arduino-ESP32 2.0.17、`esp32dev`、4 MB Flash、DIO 模式。原理图芯片名称有不一致处，板子的 PCB BOM 列出的是 ESP32-D0WD-V3，本项目据此选择经典 ESP32。不能选 ESP32-S3/C3。

成功编译后，应用固件位于 `.pio/build/esp32-debug/firmware.bin`；`platformio run -t upload` 会一并处理 bootloader、分区表和相应地址。不要把应用固件当作从地址 0 烧录的整片镜像。

## 3. 第一次调试顺序

1. 架空底盘，让四个轮子都离开桌面。接好本车匹配的电池并开启主板。原理图中电机驱动的 VM 接 `BAT1`，USB 能连上串口不代表电机电源已经开启。
2. 输入 `status`，检查引脚、`hw=OK`、`inv=0` 和 `ppr=0`。初始限幅为 100/255。
3. 输入 `scan 60 800`。应依次只有左前、右前、左后、右后转动，各 800 ms，中间暂停 400 ms。如果 60 无法起转，可在架空状态逐步改为 80 或 100。
4. 分别输入 `m fl 60 1000`、`m fr 60 1000`、`m rl 60 1000`、`m rr 60 1000`，检查每轮正方向。这里“正方向”统一定义为：轮胎顶部沿车头方向运动，也就是该轮让小车前进的转向。
5. 某轮转向相反时，输入如 `invert fr 1`，再用正 PWM 测试该轮；恢复使用 `invert fr 0`。`invert` 是直接设置 0 或 1，不是每发一次就翻转。初始四轮均为 0，程序不猜测左右电机的物理极性。
6. 输入 `save`，把四轮方向校准保存到主板；以后重启会自动加载。然后架空测试 `move fwd 60 1000`，四个轮子都应按前进方向转动。
7. 检查编码器计数和轮子旋向后，再把车放在地面，以短时间、低 PWM 测试平移与旋转。

任何时候发送 `!` 都立即将八路输出置低，无需等回车。程序会丢弃 `!` 所在行的剩余内容，**按一次回车后再发下一条命令**。正常发送 `!` 加回车即可。`stop` 或 `x` 加回车也能停止。这里的停止是撤掉驱动力并自由滑行，不是机械抱死。

## 4. 常用命令

命令不区分大小写；参数使用半角空格。下列参数 `ms` 均包含加速和换向等待的时间，允许 1～5000，省略时为 1000。`scan` 的默认时间单独为每轮 800 ms。

| 命令示例 | 作用 |
| --- | --- |
| `help` | 停止并显示帮助 |
| `status` | 查看引脚、当前目标、实际输出、计数、脉冲频率、方向校准 |
| `m fl 60 1000` | 仅左前轮以正 PWM 60 测试 1 秒，其余三轮停止 |
| `m fl -60 1000` | 左前轮反转 |
| `all 60 60 60 60 1000` | 按左前、右前、左后、右后设置四轮 PWM |
| `scan 60 800` | 按四轮顺序逐个测试，可使用负 PWM 检查反向 |
| `move fwd 60 1000` | 前进 |
| `move back 60 1000` | 后退 |
| `move left 60 1000` | 向车体左侧横移 |
| `move right 60 1000` | 向车体右侧横移 |
| `move ccw 60 1000` | 俯视逆时针旋转 |
| `move cw 60 1000` | 俯视顺时针旋转 |
| `drive 60 60 0 1000` | 前进与左移叠加，向左前方运动 |
| `drive 60 0 30 1000` | 前进与逆时针旋转叠加 |
| `limit 80` | 把四轮最大 PWM 限制为 80，修改时停止 |
| `invert fr 1` | 设置右前轮的方向反相，修改时停止 |
| `ppr all 20` | 设定四轮每圈 20 个上升沿脉冲；20 只是格式示例 |
| `ppr fl 24` | 单独设定左前轮每圈脉冲数；24 也是示例 |
| `ppr all 0` | 每圈脉冲数未知，不显示 RPM |
| `telemetry off` | 关闭每 500 ms 自动打印，方便输入命令 |
| `telemetry on` | 开启自动打印 |
| `zero` | 停止并将四个脉冲计数归零 |
| `save` | 停止并保存方向与每圈脉冲数到 NVS |
| `load` | 停止并重新读取保存值；无有效保存值时使用默认校准 |
| `defaults` | 停止并恢复内存中的默认方向、PPR 和 PWM 限幅 |
| `stop` / `x` / `!` | 停止；前两个需回车，`!` 无需等待回车 |

PWM 的输入范围为 -255～255，`move` 的速度范围为 0～255。默认 `limit=100`，约为 39% 的满占空比。所有轮速超过限幅时等比例缩放，避免破坏运动方向；例如 `all 200 100 -200 -100` 在限幅 100 时会变成 `100 50 -100 -50`。

`drive vx vy wz ms` 是无量纲的 PWM 混合接口：`vx` 正值向前，`vy` 正值向左，`wz` 正值逆时针。它不是 m/s 或 rad/s 的实测速度指令。目前没有轮径、减速比和闭环标定数据，因此没有实现速度 PID。

新运动指令会替换旧运动指令，不会把不同轮子的 `m` 命令叠加。`status` 和 `telemetry` 不延长当前运行时间。无效命令、非 ASCII 控制字节和过长输入会立即停止；错误行的剩余字节会被丢弃到换行。

`save` 仅保存方向与 PPR。PWM 限幅重启后始终恢复为 100；`defaults` 不会直接擦除已保存校准，如需保存默认值，再执行 `save`。

## 5. 脉冲输入与转速

每轮目前只有一根脉冲信号线，采用上升沿中断计数。因此 `count` 只会随脉冲增加；`pps` 和 `rpm` 均是非负速度大小，无法检测实际正反转。`out` 为施加到 H 桥的有符号电气 PWM，也不能作为实测转向。

未确认编码器规格前，保持 `ppr=0`，只观察计数和每秒脉冲数。可在停止输出后执行 `zero`，手动将车轮准确转一整圈，查看相应轮子的计数变化；再重复几圈核对。若编码器安装在电机轴上，PPR 需要把减速比换算成**输出车轮一圈的上升沿数量**，不能直接填电机轴的编码器线数。

设置正确 PPR 后，程序按 `RPM = pps × 60 / PPR` 计算转速。采样窗口为 250 ms，自动输出间隔为 500 ms，低速时读数会有量化波动。

GPIO34、35、36 没有内部上拉，本项目对四路都使用 `INPUT`。如果实际传感器是开漏输出且自身没有上拉，需要外部上拉到 3.3V；不能仅通过改成 `INPUT_PULLUP` 解决。若没有接编码器或信号悬空，跳动的计数没有测量意义。数字输入电平应与板子 3.3V 逻辑匹配。

原理图中 GPIO36 还连到 K3 按键：调试右后轮脉冲时，按这个按键也可能改变计数，避免同时使用。

## 6. 运动模型与驱动方式

X 型安装、正方向校准后，四轮顺序 FL/FR/RL/RR 的混合关系为：

```text
FL = vx - vy - wz
FR = vx + vy + wz
RL = vx + vy - wz
RR = vx - vy + wz
```

以上公式与知识文档第 5 页的坐标定义一致。该文档将 `vx=vy` 的斜移标作“右前方”，但按照它自己定义的 `vy` 向左，这应是左前方；本项目按坐标和公式实现。

SA8345 使用数据手册的 PWM mode A：一端输入 PWM，另一端保持低；两端低时自由滑行。默认 PWM 频率 1 kHz、8 位分辨率。数据手册提示此模式频率过高可能难以起转，因此调试版先使用较低频率；听到电机 PWM 声音属预期现象。

输出每 10 ms 最多增加或减小 4 个 PWM 单位；停止直接归零；由正转改为反转时先置零，至少等待 80 ms 后再逐步输出。限时是主循环检查，不是独立硬件断电保护，受到串口打印和任务调度的少量延迟影响。即使关闭串口，当前指令也会到时结束。

## 7. 项目结构与测试

```text
mecanum-debug/
  platformio.ini             固定平台、目标板、串口波特率
  include/board_config.h     接线与默认参数
  include/debug_core.h       与硬件无关的控制接口
  src/debug_core.cpp         串口命令解析、运动混合、限时和换向逻辑
  src/main.cpp               PWM、中断、串口、参数保存
  tools/project.ps1          编译、端口查询、烧录、终端、本地测试
  tests/core_test.cpp        混合比例、输入校验、超时、计时回绕、扫描与换向
  tests/firmware_test.cpp    用模拟硬件执行实际主程序，检查引脚、停止和计数
  tests/fakes/               仅本地测试使用的 Arduino / Preferences 接口
```

本地逻辑测试需要 `g++`（本机已有 MinGW-w64）：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\project.ps1 -Action test
```

固定版本的真实 ESP32 固件使用 Arduino-ESP32 2.0.17 编译。源代码同时提供 Arduino-ESP32 3.x LEDC 接口适配；本地模拟测试覆盖 2.x、3.x 两条接口路径，3.x 尚未进行真实工具链构建。

实车尚需验证：四个插口实际对应轮位、每轮转向、脉冲信号是否确实来自对应编码器、每圈脉冲数、轮子是否按 X 型安装以及地面运动表现。本地测试不会证明这些物理条件已经满足。

## 8. 依据

- 当前目录的 `原理图 (1).pdf`：四路 SA8345 的控制 GPIO、BAT1 电源和 GPIO36/K3 连接。
- 当前目录的 `PCB图.pdf`：ESP32-D0WD-V3 BOM 与四路 SA8345。
- 你提供的两张接线照片及“左前、右前、左后、右后”的接线说明。
- 当前目录的 `麦克纳姆轮知识文档.pdf`：第 5 页运动学公式；采用向前、向左、逆时针为正的定义。
- [SA8345 原厂数据手册（供应商站点提供）](https://www.sunnyqi.com/upLoad/product/month_2304/202304141609206930.pdf)：第 4、6 页真值表及 PWM mode A。
- [Espressif Arduino-ESP32 LEDC 文档](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html)。
- [Espressif GPIO 文档](https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32/api-reference/peripherals/gpio.html)：GPIO34～39 不具备内部上拉/下拉。
- [PlatformIO Espressif32 6.12.0 发布说明](https://github.com/platformio/platform-espressif32/releases/tag/v6.12.0)：Arduino-ESP32 2.0.17 依赖版本。
