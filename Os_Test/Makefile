# ===========================
# Project & toolchain
# ===========================
# BUILD_ROOT: thư mục gốc chứa mọi artifact của riêng Makefile này.
# BUILDDIR:   thư mục chứa file cuối (.elf/.bin/.map).
# OBJDIR:     nơi chứa object và dependency file theo cấu trúc source.
# LEGACYDIR:  thư mục build cũ cần dọn để tránh lẫn artifact giữa hai kiểu build.
BUILD_ROOT    := build
BUILDDIR      := $(BUILD_ROOT)/os_test
OBJDIR        := $(BUILDDIR)/obj
LEGACYDIR     := Tools
TARGET_NAME   := os_test
TARGET        := $(BUILDDIR)/$(TARGET_NAME)

CROSS         := arm-none-eabi-
CC            := $(CROSS)gcc
AS            := $(CROSS)gcc
OBJCOPY       := $(CROSS)objcopy
OBJDUMP       := $(CROSS)objdump
SIZE          := $(CROSS)size

# ===========================
# MCU / CMSIS / SPL
# ===========================
# CPUFLAGS:
# - chọn đúng kiến trúc Cortex-M3 của STM32F103.
# DEFINES:
# - kích hoạt các macro mà CMSIS/SPL mong đợi.
# INCLUDES:
# - chỉ include code của project + thư viện nền đang dùng.
CPUFLAGS      := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
DEFINES       := -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER
EXTRA_DEFINES ?=
INCLUDES      := -ICMSIS -ISPL/inc -IOS/inc -IConfig -Iapp

# ===========================
# C/ASM/LD flags
# ===========================
# CFLAGS_COMMON:
# - tối ưu O2 nhưng vẫn giữ debug symbol (-g3)
# - ff freestanding / no-builtin để phù hợp firmware bare-metal
# - function/data sections để linker có thể bỏ code không dùng
# - MMD/MP để tự sinh file dependency cho incremental build
CFLAGS_COMMON := -O2 -g3 -Wall -Wextra -Wno-unused-parameter \
	                 -ffreestanding -fno-builtin \
	                 -fno-unwind-tables -fno-asynchronous-unwind-tables \
	                 -ffunction-sections -fdata-sections \
	                 -MMD -MP
CFLAGS        := $(CPUFLAGS) $(DEFINES) $(EXTRA_DEFINES) $(INCLUDES) $(CFLAGS_COMMON)

ASFLAGS       := $(CPUFLAGS) $(DEFINES) $(EXTRA_DEFINES) $(INCLUDES) -x assembler-with-cpp

LDSCRIPT      := Linker/stm32f103.ld
LDFLAGS       := -T$(LDSCRIPT) -nostartfiles -nostdlib -static \
                 -Wl,--gc-sections -Wl,-Map=$(TARGET).map
# Nhẹ ký: dùng newlib-nano + stub syscalls "nosys"
LDFLAGS      += -specs=nano.specs -specs=nosys.specs
LDLIBS        := -Wl,--start-group -lc -lm -lgcc -Wl,--end-group

# ===========================
# Sources
# ===========================
# Chỉ liệt kê source "tự quản" của project và kéo toàn bộ SPL/src/*.c.
# Nếu sau này muốn loại bớt module SPL để giảm thời gian build,
# nên chỉnh ở vùng này đầu tiên.
SRCS_C := \
  main.c \
  Config/os_app_cfg.c \
  app/App_Task.c \
  OS/src/os_kernel.c \
  OS/src/os_sched_table.c \
  OS/src/os_port.c \
  debug/semihost.c \
  debug/syscalls_min.c \
  $(wildcard SPL/src/*.c)

SRCS_S := \
  Startup/startup_stm32f10x_md.s \
  OS/src/os_port_asm.s

# ===========================
# Objects / Deps
# ===========================
# Ánh xạ từ source path sang object path trong OBJDIR nhưng vẫn giữ cây thư mục,
# ví dụ:
#   OS/src/os_kernel.c -> build/os_test/obj/OS/src/os_kernel.o
OBJS_C := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS_C))
OBJS_S := $(patsubst %.s,$(OBJDIR)/%.o,$(SRCS_S))
OBJS   := $(OBJS_C) $(OBJS_S)
DEPS   := $(OBJS_C:.o=.d)

# ===========================
# Default goal
# ===========================
.DEFAULT_GOAL := all

# all:
# - luôn gọi prepare trước để chắc thư mục build tồn tại
# - sinh file .bin và in size cuối cùng
all: prepare $(TARGET).bin size

# prepare:
# - tạo thư mục build hiện tại
# - xóa thư mục Tools cũ nếu còn, để người dùng không nhầm artifact
prepare:
	@mkdir -p $(BUILDDIR) $(OBJDIR)
	@if [ -d "$(LEGACYDIR)" ]; then \
		echo "Removing legacy build output: $(LEGACYDIR)"; \
		rm -rf "$(LEGACYDIR)"; \
	fi

# Compile C:
# mỗi source .c sẽ có object và file .d tương ứng trong OBJDIR.
$(OBJDIR)/%.o: %.c | prepare
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble .s:
# dành cho assembly "thuần", không qua preprocessor.
$(OBJDIR)/%.o: %.s | prepare
	@mkdir -p $(dir $@)
	$(AS) $(CPUFLAGS) -c $< -o $@

# Assemble .S:
# dành cho assembly có dùng macro/preprocessor của C.
$(OBJDIR)/%.o: %.S | prepare
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# Link:
# gom toàn bộ object thành ELF cuối cùng.
$(TARGET).elf: $(OBJS) $(LDSCRIPT) | prepare
	$(CC) $(CPUFLAGS) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

# Sinh các định dạng output phục vụ flash hoặc debug.
$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

size: $(TARGET).elf
	$(SIZE) --format=berkeley $<

# list:
# tạo file disassembly + source interleave để phân tích assembly dễ hơn.
list: $(TARGET).elf
	$(OBJDUMP) -d -S $< > $(TARGET).list
# ==== cấu hình chung cho RTT ====
RTT_ADDR   ?= 0x20000000     # base SRAM F103
RTT_SIZE   ?= 0x5000         # 20 KB (F103C8)
RTT_ID     ?= SEGGER\ RTT    # chuỗi nhận diện control block
RTT_PORT   ?= 9090

# ===============================
# Nạp firmware
# ===============================
# flash:
# nạp trực tiếp ELF hiện tại qua OpenOCD + ST-Link.
.PHONY: flash
flash: $(TARGET).elf
	@test -f $< || { echo "ELF not found: $<"; exit 2; }
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
		-c "adapter speed 2000" \
		-c "reset_config none separate" \
		-c "init; halt" \
		-c "stm32f1x options_read 0" \
		-c "program {$(abspath $<)} verify" \
		-c "reset run; shutdown"

# clean:
# - xóa toàn bộ artifact của Makefile hiện tại
# - đồng thời dọn luôn Tools cũ để workspace sạch hoàn toàn
clean:
	rm -rf $(BUILDDIR) $(LEGACYDIR)

.PHONY: all clean flash size list prepare
-include $(DEPS)
