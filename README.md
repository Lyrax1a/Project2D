# Flux2D

面向 Efinix Ti60F225 与 Sapphire RISC-V 的二维渲染引擎及游戏应用。

## 工程结构

| 目录 | 内容 |
| --- | --- |
| Game | 游戏逻辑、单位、场景、菜单、存档与渲染适配 |
| Engine/Software | 命令接口、CPU 参考渲染、驱动与运行时 |
| Engine/RTL | FPGA 渲染、访存、显示及扩展逻辑 |
| Hardware/Project | Efinity 工程、外围配置、约束及厂商依赖 |
| Hardware | 位流与引导镜像 |
| Hardware/Boot | 引导加载器、升级桥接程序源码与重建脚本 |
| Assets | 图形、生成数据、音效和音乐 |
| Scripts | 构建辅助、板卡加载、输入控制与电脑音频 |
| Tests | 游戏状态轨迹与音频事件比较工具 |
| Validation | 当前版本的标识、校验与验证摘要 |

## FPGA 工程

使用 Efinity 打开 `Hardware/Project/starfield_ts2d.xml`。保持解压后的相对目录结构，工程会直接引用 `Engine/RTL`。工程版本为 Efinity 2025.2.288.3.8，器件 Ti60F225 I3。

引脚和外围配置位于同目录的 `.peri.xml`，时序约束位于 `.sdc`。DDR、HDMI 与 Sapphire 的综合输入已包含；加密 HDL 按厂商提供的形式使用，部分 IP 保留的是生成结果。编译器、器件库和授权由本机 Efinity 环境提供。

```powershell
# 将参数替换为本机的 Efinity 安装目录
powershell -NoProfile -File Scripts/build_efinity_project.ps1 -ProjectXml Hardware/Project/starfield_ts2d.xml -EfinityHome "Efinity安装目录" -ResultDir Build/FPGA
```

## 游戏构建

安装 Python 3.12 及 RISC-V Embedded GCC 8.3 工具链。在本目录执行：

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r requirements.txt
./.venv/Scripts/python.exe build.py --toolchain "RISC-V工具链的bin目录"
./.venv/Scripts/python.exe verify_game.py --toolchain "RISC-V工具链的bin目录"
./.venv/Scripts/python.exe verify_project.py
```

工具链已加入 PATH 时可省略 `--toolchain`。游戏固件输出到 `Build/game.bin`；公共软件从 Engine 直接编译，运行资源从 Assets 加载。

示例中的安装目录字符串需替换为实际路径。Python 应包含 Tkinter；构建与校验不需要连接板卡。图集及生成头文件已经包含，编译无需额外的资源导出工程。

## 离线回归与引导重建

```powershell
# 使用包内基准摘要验证 30000 tick 游戏状态和音频事件
./.venv/Scripts/python.exe Tests/compare_gameplay.py
# 重建并核对引导加载器及升级桥，不操作板卡
./.venv/Scripts/python.exe Hardware/Boot/build.py --toolchain "RISC-V工具链的bin目录"
```

游戏回归需要 Visual Studio 2022 的 C++/MSVC 与 Windows SDK。默认查找 Community 安装位置，其他安装用 `--vcvars "vcvars64.bat完整路径"` 指定。无需包外旧游戏工程；可选 `--baseline` 用于额外的版本比较。产物写入 Build。

## 运行

```powershell
# 连接已加载的游戏
./Start.ps1
# 加载位流、固件与资源后连接控制窗口
./Start.ps1 -Load -Port COM9 -EfinityHome "Efinity安装目录"
# 禁用电脑音频
./Start.ps1 -NoAudio
```

串口应按实际设备填写。`-Load` 使用易失 JTAG/DDR 加载，不写 Flash；启动前应保存需要保留的游戏档案。音效和音乐由电脑配套工具播放。

菜单包含“开始游戏、新建档案、设置”和“L 编队配置”。WASD 移动，鼠标瞄准，左键/F使用，右键/T标记，滚轮/V切换，Enter部署。

## 验证与版本

当前版本的构建和测试摘要见 `Validation/results.json`。校验涵盖源码引用、资源、位流与固件标识；不替代 FPGA 重新综合或实机测试。历史压力场景成绩不作为本游戏版本的帧率成绩。历史实验、录像及过程日志单独保留在既有项目归档中，不属于本运行工程。

游戏、代码及资源的来源说明见 `THIRD_PARTY_NOTICES.md`，源文件中的许可声明保持有效。
