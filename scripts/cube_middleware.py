"""Dołącza do builda middleware z framework-stm32cubef7: FreeRTOS (CMSIS-RTOS2), lwIP i sterownik PHY LAN8742.

Framework PlatformIO stm32cube buduje automatycznie tylko HAL/BSP/USB, dlatego te źródła
dodajemy tutaj. Konfiguracja (FreeRTOSConfig.h, lwipopts.h) leży w katalogu include/ projektu.
"""
import os

Import("env")  # noqa: F821  (dostarczane przez SCons/PlatformIO)

fw = env.PioPlatform().get_package_dir("framework-stm32cubef7")  # noqa: F821
rtos = os.path.join(fw, "Middlewares", "Third_Party", "FreeRTOS", "Source")
lwip = os.path.join(fw, "Middlewares", "Third_Party", "LwIP")
phy = os.path.join(fw, "Drivers", "BSP", "Components", "lan8742")
bsp = os.path.join(fw, "Drivers", "BSP", "STM32746G-Discovery")
touch = os.path.join(fw, "Drivers", "BSP", "Components", "ft5336")

# Port FreeRTOS ARM_CM7 wymaga FPU (fpv5-sp-d16); flagi dla całego builda, żeby ABI było spójne.
FPU_FLAGS = ["-mfpu=fpv5-sp-d16", "-mfloat-abi=hard"]
env.Append(CCFLAGS=FPU_FLAGS, ASFLAGS=FPU_FLAGS, LINKFLAGS=FPU_FLAGS)  # noqa: F821

env.Append(  # noqa: F821
    CPPPATH=[
        "$PROJECT_INCLUDE_DIR",
        os.path.join(rtos, "include"),
        os.path.join(rtos, "portable", "GCC", "ARM_CM7", "r0p1"),
        os.path.join(rtos, "CMSIS_RTOS_V2"),
        os.path.join(lwip, "src", "include"),
        os.path.join(lwip, "system"),
        phy,
        bsp,
    ]
)

env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "FreeRTOS"),
    rtos,
    src_filter=[
        "-<*>",
        "+<*.c>",
        "+<portable/GCC/ARM_CM7/r0p1/port.c>",
        "+<portable/MemMang/heap_4.c>",
        "+<CMSIS_RTOS_V2/cmsis_os2.c>",
    ],
)

env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "LwIP"),
    lwip,
    src_filter=[
        "-<*>",
        "+<src/core/*.c>",
        "+<src/core/ipv4/*.c>",
        "+<src/api/*.c>",
        "+<src/netif/ethernet.c>",
        "+<system/OS/sys_arch.c>",
    ],
)

env.BuildSources(os.path.join("$BUILD_DIR", "LAN8742"), phy, src_filter=["+<lan8742.c>"])  # noqa: F821

# BSP płytki: SDRAM (FMC), LCD (LTDC + PLLSAI), dotyk FT5336 (I2C3), QSPI flash (N25Q128A)
env.BuildSources(  # noqa: F821
    os.path.join("$BUILD_DIR", "BSP_Disco"),
    bsp,
    src_filter=[
        "-<*>",
        "+<stm32746g_discovery.c>",
        "+<stm32746g_discovery_sdram.c>",
        "+<stm32746g_discovery_lcd.c>",
        "+<stm32746g_discovery_ts.c>",
        "+<stm32746g_discovery_qspi.c>",
    ],
)
env.BuildSources(os.path.join("$BUILD_DIR", "FT5336"), touch, src_filter=["+<ft5336.c>"])  # noqa: F821
