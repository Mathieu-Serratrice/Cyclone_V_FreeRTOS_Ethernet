#==============================================================================
#
#	RTOSDemo project
#
#==============================================================================

# The file "user_config.mk" is excluded from GIT.
# It should define "GCC_BIN", which is the location of the compiler
# and linker.

#include user_config.mk


MY_BOARD=CYCLONE_V_SOC_DE10

# Example contents of user_config.mk :
GCC_BIN=C:/intelFPGA_pro/17.0/embedded/host_tools/mentor/gnu/arm/baremetal/bin
GCC_PREFIX=arm-altera-eabi

#include user_config.mk

USE_TICKLESS_IDLE = false
USE_MICREL_KSZ8851 ?= false

USE_IPERF=false
USE_FREERTOS_FAT=true
USE_USB_CDC=false
USE_TELNET=false
USE_USART=true
USE_LOG_EVENT=false
ipconfigMULTI_INTERFACE=false

C_SRCS=
CPP_SRCS=
S_SRCS=
DEFS=

# Base paths
# PRJ_PATH points to e:\home
PRJ_PATH  = $(subst /fireworks,,$(abspath ../fireworks))

# CUR_PATH points to e:\home\cyclone_hein
CUR_PATH  = $(subst /fireworks,,$(abspath ./fireworks))

MY_PATH = $(CUR_PATH)

FREERTOS_ROOT_PATH = $(CUR_PATH)/FreeRTOS
COMMON_UTILS_PATH = $(FREERTOS_ROOT_PATH)/utilities
FREERTOS_PATH = $(FREERTOS_ROOT_PATH)/FreeRTOS_v10.0.0

RTOS_MEM_PATH = $(FREERTOS_PATH)/portable/MemMang
PLUS_FAT_PATH = $(FREERTOS_ROOT_PATH)/FreeRTOS-Plus-FAT
UTILITIES_PATH = $(FREERTOS_ROOT_PATH)/utilities

HW_LIB=$(CUR_PATH)/Altera_Code/HardwareLibrary
SOC_SUPPORT=$(CUR_PATH)/Altera_Code/SoCSupport

ifeq ($(ipconfigMULTI_INTERFACE),true)
	DEFS += -DipconfigMULTI_INTERFACE=1
	PLUS_TCP_PATH = $(FREERTOS_ROOT_PATH)/FreeRTOS-Plus-TCP-multi
else
	DEFS += -DipconfigMULTI_INTERFACE=0
	PLUS_TCP_PATH = $(FREERTOS_ROOT_PATH)/FreeRTOS-Plus-TCP
endif

DEFS += -DBOARD=$(MY_BOARD) -DFREERTOS_USED

INC_PATH = 

# Include path
INC_PATH += \
	$(MY_PATH)/ \
	$(FREERTOS_PATH)/ \
	$(FREERTOS_PATH)/include/ \
	$(FREERTOS_PATH)/portable/GCC/ARM_CA9/ \
	$(HW_LIB)/include/ \
	$(SOC_SUPPORT)/include/ \
	$(CUR_PATH)/Demo/Common/include/ \
	$(PLUS_TCP_PATH)/ \
	$(PLUS_TCP_PATH)/include/ \
	$(PLUS_TCP_PATH)/portable/compiler/GCC/ \
	$(PLUS_TCP_PATH)/portable\NetworkInterface\Cyclone_V_SoC/ \
	$(PLUS_TCP_PATH)/Protocols/include/ \
	$(COMMON_UTILS_PATH)/include/ \
	$(UTILITIES_PATH)/include/ \
	$(UTILITIES_PATH)/

# S_SRCS

C_SRCS += \
	main.c \
	LEDs.c
#	reg_test.c

C_SRCS += \
	$(FREERTOS_PATH)/tasks.c \
	$(FREERTOS_PATH)/queue.c \
	$(FREERTOS_PATH)/list.c \
	$(FREERTOS_PATH)/event_groups.c \
	$(FREERTOS_PATH)/portable/MemMang/heap_4.c \
	$(FREERTOS_PATH)/portable/GCC/ARM_CA9/port.c \
	$(FREERTOS_PATH)/portable/GCC/ARM_CA9/portASM.c

C_SRCS += \
	$(HW_LIB)/alt_16550_uart.c \
	$(HW_LIB)/alt_address_space.c \
	$(HW_LIB)/alt_bridge_manager.c \
	$(HW_LIB)/alt_cache.c \
	$(HW_LIB)/alt_clock_manager.c \
	$(HW_LIB)/alt_dma.c \
	$(HW_LIB)/alt_dma_program.c \
	$(HW_LIB)/alt_ecc.c \
	$(HW_LIB)/alt_fpga_manager.c \
	$(HW_LIB)/alt_generalpurpose_io.c \
	$(HW_LIB)/alt_globaltmr.c \
	$(HW_LIB)/alt_i2c.c \
	$(HW_LIB)/alt_interrupt.c \
	$(HW_LIB)/alt_mmu.c \
	$(HW_LIB)/alt_nand.c \
	$(HW_LIB)/alt_qspi.c \
	$(HW_LIB)/alt_reset_manager.c \
	$(HW_LIB)/alt_sdmmc.c \
	$(HW_LIB)/alt_spi.c \
	$(HW_LIB)/alt_system_manager.c \
	$(HW_LIB)/alt_timers.c \
	$(HW_LIB)/alt_watchdog.c

C_SRCS += \
	$(SOC_SUPPORT)/cache_support.c \
	$(SOC_SUPPORT)/fpga_support.c \
	$(SOC_SUPPORT)/mmu_support.c \
	$(SOC_SUPPORT)/uart0_support.c

# "USE_TICKLESS_IDLE" if true then tickless idle will be used
ifeq ($(USE_TICKLESS_IDLE),true)
	DEFS += -DconfigUSE_TICKLESS_IDLE=1
	C_SRCS += \
		$(FREERTOS_PATH)/portable/gcc/sam4e/tc_timer.c
endif

C_SRCS += \
	$(PLUS_TCP_PATH)/FreeRTOS_ARP.c \
	$(PLUS_TCP_PATH)/FreeRTOS_DHCP.c \
	$(PLUS_TCP_PATH)/FreeRTOS_DNS.c \
	$(PLUS_TCP_PATH)/FreeRTOS_IP.c \
	$(PLUS_TCP_PATH)/FreeRTOS_Sockets.c \
	$(PLUS_TCP_PATH)/FreeRTOS_Stream_Buffer.c \
	$(PLUS_TCP_PATH)/FreeRTOS_TCP_IP.c \
	$(PLUS_TCP_PATH)/FreeRTOS_TCP_WIN.c \
	$(PLUS_TCP_PATH)/FreeRTOS_UDP_IP.c \
	$(PLUS_TCP_PATH)/portable/NetworkInterface/Cyclone_V_SoC/NetworkInterface.c \
	$(PLUS_TCP_PATH)/portable/NetworkInterface/Cyclone_V_SoC/cyclone_dma.c \
	$(PLUS_TCP_PATH)/portable/NetworkInterface/Cyclone_V_SoC/cyclone_emac.c \
	$(PLUS_TCP_PATH)/portable/NetworkInterface/Cyclone_V_SoC/cyclone_phy.c \
	$(PLUS_TCP_PATH)/portable/BufferManagement/BufferAllocation_1.c

ifeq ($(ipconfigMULTI_INTERFACE),true)
	C_SRCS += \
		$(PLUS_TCP_PATH)/FreeRTOS_Routing.c
endif

ifeq ($(USE_USART),true)
	DEFS += -DUSE_USART=1
	C_SRCS += \
		$(CUR_PATH)/serial.c
endif

#    DEFS += -D SIMPLE_MEMCPY=0
#    DEFS += -D SIMPLE_MEMSET=0
#    C_SRCS += \
#     	$(COMMON_UTILS_PATH)/memcpy.c

#C_SRCS += \
#	$(COMMON_UTILS_PATH)/memcpy_simple.c


C_SRCS += \
	$(COMMON_UTILS_PATH)/UDPLoggingPrintf.c \
	$(COMMON_UTILS_PATH)/printf-stdarg.c

ifeq ($(USE_IPERF),true)
	DEFS += -D USE_IPERF=1
	C_SRCS += \
		$(COMMON_UTILS_PATH)/iperf_task_v3_0c.c
endif

C_SRCS += \
	$(UTILITIES_PATH)/hr_gettime.c

ifeq ($(USE_LOG_EVENT),true)
	DEFS += -D USE_LOG_EVENT=1
	DEFS += -D LOG_EVENT_NAME_LEN=32
	DEFS += -D LOG_EVENT_COUNT=512
	C_SRCS += \
		$(UTILITIES_PATH)/eventLogging.c
else
	DEFS += -D USE_LOG_EVENT=0
endif

ifeq ($(USE_TELNET),true)
	DEFS += -D USE_TELNET=1
	C_SRCS += \
		$(UTILITIES_PATH)/telnet.c
else
	DEFS += -D USE_TELNET=0
endif

ifeq ($(USE_FREERTOS_FAT),true)
	INC_PATH += \
		$(PLUS_FAT_PATH)/include/ \
		$(PLUS_FAT_PATH)/portable/common/
	C_SRCS += \
		$(PLUS_FAT_PATH)/ff_crc.c \
		$(PLUS_FAT_PATH)/ff_dir.c \
		$(PLUS_FAT_PATH)/ff_error.c \
		$(PLUS_FAT_PATH)/ff_fat.c \
		$(PLUS_FAT_PATH)/ff_file.c \
		$(PLUS_FAT_PATH)/ff_ioman.c \
		$(PLUS_FAT_PATH)/ff_memory.c \
		$(PLUS_FAT_PATH)/ff_format.c \
		$(PLUS_FAT_PATH)/ff_string.c \
		$(PLUS_FAT_PATH)/ff_stdio.c \
		$(PLUS_FAT_PATH)/ff_sys.c \
		$(PLUS_FAT_PATH)/ff_time.c \
		$(PLUS_FAT_PATH)/ff_locking.c \
		$(PLUS_TCP_PATH)/protocols/FTP/FreeRTOS_FTP_commands.c \
		$(PLUS_TCP_PATH)/protocols/FTP/FreeRTOS_FTP_server.c \
		$(PLUS_TCP_PATH)/protocols/HTTP/FreeRTOS_HTTP_commands.c \
		$(PLUS_TCP_PATH)/protocols/HTTP/FreeRTOS_HTTP_server.c \
		$(PLUS_TCP_PATH)/protocols/Common/FreeRTOS_TCP_server.c \
		$(PLUS_FAT_PATH)/portable/common/ff_ramdisk.c \
		$(UTILITIES_PATH)/date_and_time.c

	DEFS += -D USE_FREERTOS_FAT=1
endif

DEFS += -D ALT_INT_PROVISION_VECTOR_SUPPORT=0

LINKER_SCRIPT=$(CUR_PATH)/cycloneV-dk-ram.ld

TARGET = RTOSDemo.elf

# Later, use -Os
OPTIMIZATION = -O0
# OPTIMIZATION = -O0

#HT Do not use it for now
DEBUG=-g3

C_EXTRA_FLAGS= \
	-Wall \
	-fmessage-length=0 \
	-mfloat-abi=softfp \
	-mtune=cortex-a9 \
	-mcpu=cortex-a9 \
	-march=armv7-a \
	-std=c99 \
	-fno-builtin-memcpy \
	-fno-builtin-memset \
	-fdata-sections \
	-ffunction-sections \
	-mfpu=neon \
	-mno-unaligned-access

#	-fno-strict-aliasing

AS_EXTRA_FLAGS= \
	-mfloat-abi=softfp \
	-mtune=cortex-a9 \
	-mcpu=cortex-a9 \
	-march=armv7-a \
	-mfpu=neon

C_ONLY_FLAGS=

LD_EXTRA_FLAGS= \
	-Xlinker --defsym=__cs3_isr_irq=FreeRTOS_IRQ_Handler \
	-Xlinker --defsym=__cs3_isr_swi=FreeRTOS_SWI_Handler \
	-Xlinker -Map=RTOSDemo.map \
	-Xlinker --gc-sections \
	-Xlinker --allow-multiple-definition
