# arm-linux-gnueabihf.toolchain.cmake
# 适配 Linaro GCC 4.9-2014.09 (armv7-a / cortex-a9 / hard-FP)
# 工具链根: /home/sc11080/ToolChain/gcc-linaro-arm-linux-gnueabihf-4.9-2014.09_linux
#
# 用法:
#   cmake -S . -B build-armhf -DCMAKE_TOOLCHAIN_FILE=arm-linux-gnueabihf.toolchain.cmake
#   cmake --build build-armhf -j4

# 目标系统: Linux 用户态 (RTOS 改成 Generic)
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)     # 32位 ARM

# 编译器(你 PATH 里已有 arm-linux-gnueabihf-gcc,直接写名字即可;
# 若想保险用绝对路径,改成 /home/sc11080/ToolChain/.../bin/arm-linux-gnueabihf-gcc)
set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 工具链根目录: 本机使用的是发行版交叉工具链 (Ubuntu 的 arm-linux-gnueabihf-gcc,
# 自带 sysroot), 不使用 Linaro 的 find-root 树。
# 原配置为:
#   set(CMAKE_FIND_ROOT_PATH /home/sc11080/ToolChain/gcc-linaro-arm-linux-gnueabihf-4.9-2014.09_linux)
# 该路径在本机不存在, 且会把 find_package / find_library 误导到一个空的根, 已删除。
# 库与头文件由交叉编译器自带的 sysroot 解析, 因此下面这组 MODE 约束也不再需要:
#   CMAKE_FIND_ROOT_PATH_MODE_{PROGRAM,LIBRARY,INCLUDE,PACKAGE}
# （若将来确实要指定一个 find-root 树, 请把根路径与被约束的 MODE 一起加回来。）

# 注意: 该工具链编译时已内置 --with-arch=armv7-a --with-tune=cortex-a9
#       --with-fpu=vfpv3-d16 --with-float=hard,所以不需要再传 -march/-mfpu。
# 若以后要切换到别的 SoC(如 cortex-a8 / neon 等),在这里加:
# set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a9 -mfpu=vfpv3-d16 -mfloat-abi=hard")
