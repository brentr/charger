source ../stm32l-discovery/target.gdb
load
monitor reset halt
tbreak main
continue
