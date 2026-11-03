# 渲染引擎

Software 提供 32B 命令接口、CPU 参考渲染、通用驱动、GPU 提交与板级运行时。游戏采用 Immediate 执行路径。

RTL 由 Hardware/Project/starfield_ts2d.xml 直接引用。frame_renderer_pb3.sv 实现 Immediate/Tile 渲染；memory 目录实现 AXI 搬运；platform/board_conformance/rtl 包含 CSR、帧缓冲和扫描输出；hw/rtl 包含三角形与音频模块。

公共代码的源文件许可与外部算法引用应随派生版本保留。
