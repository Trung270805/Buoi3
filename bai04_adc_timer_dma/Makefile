TARGET = main

CC = arm-none-eabi-gcc
AS = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

CFLAGS = -mcpu=cortex-m3 \
         -mthumb \
         -O0 \
         -Wall \
         -Wextra \
         -ffunction-sections \
         -fdata-sections \
         -DSTM32F103xB

CPPFLAGS = \
         -I. \
         -I../STM32CubeF1/Drivers/CMSIS/Include \
         -I../STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Include \
         -I../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Inc

LDFLAGS = -T linker.ld \
          -mcpu=cortex-m3 \
          -mthumb \
          -Wl,--gc-sections \
          -Wl,-Map=$(TARGET).map \
          --specs=nosys.specs \
          --specs=nano.specs

HAL_SRC = \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc_ex.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c \
    ../STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c

all: $(TARGET).bin

main.o: main.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c main.c -o main.o

system_stm32f1xx.o: system_stm32f1xx.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c system_stm32f1xx.c -o system_stm32f1xx.o

startup.o: startup.s
	$(AS) $(CFLAGS) $(CPPFLAGS) -c startup.s -o startup.o

hal.o: $(HAL_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $(HAL_SRC)

$(TARGET).elf: main.o system_stm32f1xx.o startup.o hal.o
	$(CC) $(CFLAGS) $(LDFLAGS) \
		main.o \
		system_stm32f1xx.o \
		startup.o \
		stm32f1xx_hal.o \
		stm32f1xx_hal_cortex.o \
		stm32f1xx_hal_gpio.o \
		stm32f1xx_hal_rcc.o \
		stm32f1xx_hal_rcc_ex.o \
		stm32f1xx_hal_tim.o \
		stm32f1xx_hal_tim_ex.o \
		stm32f1xx_hal_adc.o \
		stm32f1xx_hal_adc_ex.o \
		stm32f1xx_hal_dma.o \
		stm32f1xx_hal_uart.o \
		-o $(TARGET).elf

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin
	$(SIZE) $(TARGET).elf

clean:
	rm -f *.o *.elf *.bin *.map
