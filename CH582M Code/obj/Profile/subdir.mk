################################################################################
# MRS Version: 2.2.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Profile/battservice.c \
../Profile/devinfoservice.c \
../Profile/heartrateservice.c 

C_DEPS += \
./Profile/battservice.d \
./Profile/devinfoservice.d \
./Profile/heartrateservice.d 

OBJS += \
./Profile/battservice.o \
./Profile/devinfoservice.o \
./Profile/heartrateservice.o 


EXPANDS += \
./Profile/battservice.c.234r.expand \
./Profile/devinfoservice.c.234r.expand \
./Profile/heartrateservice.c.234r.expand 



# Each subdirectory must supply rules for building sources it contributes
Profile/%.o: ../Profile/%.c
	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DDEBUG=1 -I"c:\Users\Water\Documents\CH582M Projects\ContinuousHealthMonitorChip\StdPeriphDriver\inc" -I"c:\Users\Water\Documents\CH582M Projects\ContinuousHealthMonitorChip\RVMSIS" -I"c:/Users/Water/Documents/CH582M Projects/ContinuousHealthMonitorChip/HAL/include" -I"c:/Users/Water/Documents/CH582M Projects/ContinuousHealthMonitorChip/LIB" -I"c:/Users/Water/Documents/CH582M Projects/ContinuousHealthMonitorChip/Profile/include" -I"c:/Users/Water/Documents/CH582M Projects/ContinuousHealthMonitorChip/APP/include" -I"c:/Users/Water/Documents/CH582M Projects/ContinuousHealthMonitorChip/Startup" -I"c:/Users/Water/Documents/CH582M Projects/ContinuousHealthMonitorChip/Ld" -I"c:/Users/Water/Documents/CH582M Projects/ContinuousHealthMonitorChip/RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

