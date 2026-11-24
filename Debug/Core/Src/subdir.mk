################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/CarButton.c \
../Core/Src/CarEngine.c \
../Core/Src/CarPower.c \
../Core/Src/CarSensors.c \
../Core/Src/CarStarter.c \
../Core/Src/CarStorage.c \
../Core/Src/Communication.c \
../Core/Src/CommunicationDevices.c \
../Core/Src/CommunicationLink.c \
../Core/Src/Crypto.c \
../Core/Src/DeviceKeys.c \
../Core/Src/NrfLink.c \
../Core/Src/SecureCommunication.c \
../Core/Src/SecureProtocol.c \
../Core/Src/SecureSessionStorage.c \
../Core/Src/SecureTransport.c \
../Core/Src/UsbDebug.c \
../Core/Src/W25Q64.c \
../Core/Src/adc.c \
../Core/Src/dma.c \
../Core/Src/ds18b20.c \
../Core/Src/gpio.c \
../Core/Src/main.c \
../Core/Src/nRF24L01.c \
../Core/Src/ow.c \
../Core/Src/rtc.c \
../Core/Src/spi.c \
../Core/Src/stm32f4xx_hal_msp.c \
../Core/Src/stm32f4xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f4xx.c \
../Core/Src/tim.c \
../Core/Src/tiny_aes.c 

OBJS += \
./Core/Src/CarButton.o \
./Core/Src/CarEngine.o \
./Core/Src/CarPower.o \
./Core/Src/CarSensors.o \
./Core/Src/CarStarter.o \
./Core/Src/CarStorage.o \
./Core/Src/Communication.o \
./Core/Src/CommunicationDevices.o \
./Core/Src/CommunicationLink.o \
./Core/Src/Crypto.o \
./Core/Src/DeviceKeys.o \
./Core/Src/NrfLink.o \
./Core/Src/SecureCommunication.o \
./Core/Src/SecureProtocol.o \
./Core/Src/SecureSessionStorage.o \
./Core/Src/SecureTransport.o \
./Core/Src/UsbDebug.o \
./Core/Src/W25Q64.o \
./Core/Src/adc.o \
./Core/Src/dma.o \
./Core/Src/ds18b20.o \
./Core/Src/gpio.o \
./Core/Src/main.o \
./Core/Src/nRF24L01.o \
./Core/Src/ow.o \
./Core/Src/rtc.o \
./Core/Src/spi.o \
./Core/Src/stm32f4xx_hal_msp.o \
./Core/Src/stm32f4xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f4xx.o \
./Core/Src/tim.o \
./Core/Src/tiny_aes.o 

C_DEPS += \
./Core/Src/CarButton.d \
./Core/Src/CarEngine.d \
./Core/Src/CarPower.d \
./Core/Src/CarSensors.d \
./Core/Src/CarStarter.d \
./Core/Src/CarStorage.d \
./Core/Src/Communication.d \
./Core/Src/CommunicationDevices.d \
./Core/Src/CommunicationLink.d \
./Core/Src/Crypto.d \
./Core/Src/DeviceKeys.d \
./Core/Src/NrfLink.d \
./Core/Src/SecureCommunication.d \
./Core/Src/SecureProtocol.d \
./Core/Src/SecureSessionStorage.d \
./Core/Src/SecureTransport.d \
./Core/Src/UsbDebug.d \
./Core/Src/W25Q64.d \
./Core/Src/adc.d \
./Core/Src/dma.d \
./Core/Src/ds18b20.d \
./Core/Src/gpio.d \
./Core/Src/main.d \
./Core/Src/nRF24L01.d \
./Core/Src/ow.d \
./Core/Src/rtc.d \
./Core/Src/spi.d \
./Core/Src/stm32f4xx_hal_msp.d \
./Core/Src/stm32f4xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f4xx.d \
./Core/Src/tim.d \
./Core/Src/tiny_aes.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/CarButton.cyclo ./Core/Src/CarButton.d ./Core/Src/CarButton.o ./Core/Src/CarButton.su ./Core/Src/CarEngine.cyclo ./Core/Src/CarEngine.d ./Core/Src/CarEngine.o ./Core/Src/CarEngine.su ./Core/Src/CarPower.cyclo ./Core/Src/CarPower.d ./Core/Src/CarPower.o ./Core/Src/CarPower.su ./Core/Src/CarSensors.cyclo ./Core/Src/CarSensors.d ./Core/Src/CarSensors.o ./Core/Src/CarSensors.su ./Core/Src/CarStarter.cyclo ./Core/Src/CarStarter.d ./Core/Src/CarStarter.o ./Core/Src/CarStarter.su ./Core/Src/CarStorage.cyclo ./Core/Src/CarStorage.d ./Core/Src/CarStorage.o ./Core/Src/CarStorage.su ./Core/Src/Communication.cyclo ./Core/Src/Communication.d ./Core/Src/Communication.o ./Core/Src/Communication.su ./Core/Src/CommunicationDevices.cyclo ./Core/Src/CommunicationDevices.d ./Core/Src/CommunicationDevices.o ./Core/Src/CommunicationDevices.su ./Core/Src/CommunicationLink.cyclo ./Core/Src/CommunicationLink.d ./Core/Src/CommunicationLink.o ./Core/Src/CommunicationLink.su ./Core/Src/Crypto.cyclo ./Core/Src/Crypto.d ./Core/Src/Crypto.o ./Core/Src/Crypto.su ./Core/Src/DeviceKeys.cyclo ./Core/Src/DeviceKeys.d ./Core/Src/DeviceKeys.o ./Core/Src/DeviceKeys.su ./Core/Src/NrfLink.cyclo ./Core/Src/NrfLink.d ./Core/Src/NrfLink.o ./Core/Src/NrfLink.su ./Core/Src/SecureCommunication.cyclo ./Core/Src/SecureCommunication.d ./Core/Src/SecureCommunication.o ./Core/Src/SecureCommunication.su ./Core/Src/SecureProtocol.cyclo ./Core/Src/SecureProtocol.d ./Core/Src/SecureProtocol.o ./Core/Src/SecureProtocol.su ./Core/Src/SecureSessionStorage.cyclo ./Core/Src/SecureSessionStorage.d ./Core/Src/SecureSessionStorage.o ./Core/Src/SecureSessionStorage.su ./Core/Src/SecureTransport.cyclo ./Core/Src/SecureTransport.d ./Core/Src/SecureTransport.o ./Core/Src/SecureTransport.su ./Core/Src/UsbDebug.cyclo ./Core/Src/UsbDebug.d ./Core/Src/UsbDebug.o ./Core/Src/UsbDebug.su ./Core/Src/W25Q64.cyclo ./Core/Src/W25Q64.d ./Core/Src/W25Q64.o ./Core/Src/W25Q64.su ./Core/Src/adc.cyclo ./Core/Src/adc.d ./Core/Src/adc.o ./Core/Src/adc.su ./Core/Src/dma.cyclo ./Core/Src/dma.d ./Core/Src/dma.o ./Core/Src/dma.su ./Core/Src/ds18b20.cyclo ./Core/Src/ds18b20.d ./Core/Src/ds18b20.o ./Core/Src/ds18b20.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/nRF24L01.cyclo ./Core/Src/nRF24L01.d ./Core/Src/nRF24L01.o ./Core/Src/nRF24L01.su ./Core/Src/ow.cyclo ./Core/Src/ow.d ./Core/Src/ow.o ./Core/Src/ow.su ./Core/Src/rtc.cyclo ./Core/Src/rtc.d ./Core/Src/rtc.o ./Core/Src/rtc.su ./Core/Src/spi.cyclo ./Core/Src/spi.d ./Core/Src/spi.o ./Core/Src/spi.su ./Core/Src/stm32f4xx_hal_msp.cyclo ./Core/Src/stm32f4xx_hal_msp.d ./Core/Src/stm32f4xx_hal_msp.o ./Core/Src/stm32f4xx_hal_msp.su ./Core/Src/stm32f4xx_it.cyclo ./Core/Src/stm32f4xx_it.d ./Core/Src/stm32f4xx_it.o ./Core/Src/stm32f4xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f4xx.cyclo ./Core/Src/system_stm32f4xx.d ./Core/Src/system_stm32f4xx.o ./Core/Src/system_stm32f4xx.su ./Core/Src/tim.cyclo ./Core/Src/tim.d ./Core/Src/tim.o ./Core/Src/tim.su ./Core/Src/tiny_aes.cyclo ./Core/Src/tiny_aes.d ./Core/Src/tiny_aes.o ./Core/Src/tiny_aes.su

.PHONY: clean-Core-2f-Src

