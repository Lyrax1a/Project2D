# 引导与升级桥接程序

boot.c 实现升级后的 UART/DDR 固件加载器；usurper_boot_upgrade.c 将该加载器写入可写引导 RAM，校验后跳转。其余文件为启动代码、运行时、链接布局和嵌入的引导镜像。

在工程根目录执行：

```powershell
python Hardware/Boot/build.py --toolchain "RISC-V工具链的bin目录"
```

产物位于 Build/Boot。脚本会校验重建的加载器、升级桥与现有 Hardware/boot_upgrade.json 标识相符，不会上传或替换已分发的镜像。这些源码对应升级加载器；FPGA 原始 RAM 初始化文件仍由 Hardware/Project 保存。
