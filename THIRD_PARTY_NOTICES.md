# 第三方代码与资源

## Usurper

游戏行为及部分图形、字体、音频资源来自 Usurper 参考工程，原游戏作者为 **Raven**（游戏创意、程序、美术）。本工程包含针对 FPGA/RISC-V 的移植与渲染适配，不将原游戏内容或第三方素材声明为全部原创。资源整理不改变原有权利归属。

## 音乐

原工程制作名单记录如下，适用范围以各素材原始许可为准：

- Chiphead64 — Space Riddle（CC BY 4.0）
- Pizza Doggy — Cozy Tunes
- DavidKBD — Code Injection / Pink Bloom（CC BY 4.0）
- Oicanji — Welcome to the Elevator（CC BY 4.0）

音频清单及文件摘要位于 Assets/Audio。本工程不为未附统一许可的第三方资源授予额外再分发权利。

## Godot 与算法代码

部分几何、排序和数值处理依据 Godot 实现进行适配。usurper_polyline.c、usurper_threat_sort.c 等文件保留原有版权、许可与技术出处。相关署名包括 Godot Engine contributors、Juan Linietsky 和 Ariel Manzur。源文件中的完整许可条款为准。

## FPGA 厂商与工具依赖

Hardware/Project 包含 Efinix 平台的 DDR、HDMI、Sapphire 与相关生成文件。原有文件头、加密内容和使用条件保持不变。Efinity、RISC-V 工具链、设备驱动及其许可证不随本工程分发。

Python 运行依赖见 requirements.txt，各依赖遵循自身许可。本工程不对全部文件施加统一的原创或开放许可声明。
