source ~/stm32l-discovery/cmds.gdb
source ~/stm32l-discovery/tcp/target.gdb
load
monitor reset halt
tbreak main
continue
