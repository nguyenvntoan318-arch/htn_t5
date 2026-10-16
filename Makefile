PROJECT = t5b1

PREFIX = arm-none-eabi-

CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

CPU = -mcpu=cortex-m3 -mthumb

CFLAGS = $(CPU) \
         -std=gnu11 \
         -O1 \
         -g3 \
         -Wall \
         -Wextra \
         -ffunction-sections \
         -fdata-sections \
         -fno-common \
         -DSTM32F103xB \
         -DUSE_FreeRTOS \
         -DHSE_VALUE=8000000U

ASFLAGS = $(CPU) \
          -g3 \
          -x assembler-with-cpp

LDFLAGS = $(CPU) \
          -TSTM32F103C8Tx_FLASH.ld \
          -Wl,--gc-sections \
          -Wl,-Map=build/$(PROJECT).map \
          --specs=nano.specs \
          --specs=nosys.specs \
          -static \
          -Wl,--start-group \
          -lc \
          -lm \
          -Wl,--end-group

INCLUDES = -ICore \
           -IDrivers/CMSIS \
           -I$(HOME)/STM32CubeF1/Drivers/CMSIS/Include \
           -I$(HOME)/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Include \
           -IFreeRTOS \
           -IFreeRTOS/include \
           -IFreeRTOS/portable/GCC/ARM_CM3

CFLAGS += $(INCLUDES)

C_SOURCES = Core/main.c \
            FreeRTOS/tasks.c \
            FreeRTOS/queue.c \
            FreeRTOS/list.c \
            FreeRTOS/port.c \
            FreeRTOS/heap_4.c

ASM_SOURCES = Core/startup_stm32f103xb.s
ASM_SOURCES = Core/startup_stm32f103xb.s

C_OBJECTS = $(patsubst %.c,build/%.o,$(C_SOURCES))
ASM_OBJECTS = $(patsubst %.s,build/%.o,$(ASM_SOURCES))

OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

.PHONY: all clean flash size

all: build/$(PROJECT).elf build/$(PROJECT).bin size

build/$(PROJECT).elf: $(OBJECTS) STM32F103C8Tx_FLASH.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "BUILD ELF SUCCESS: $@"

build/$(PROJECT).bin: build/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@
	@echo "BUILD BIN SUCCESS: $@"

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

size: build/$(PROJECT).elf
	$(SIZE) $<

clean:
	rm -rf build

flash: build/$(PROJECT).elf
	openocd -f interface/stlink.cfg \
	        -f target/stm32f1x.cfg \
	        -c "program build/$(PROJECT).elf verify reset exit"
