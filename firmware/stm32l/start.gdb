source ../stm32l/target.gdb
load
monitor reset halt
tbreak main
continue
