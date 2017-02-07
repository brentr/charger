target extended-remote | openocd -c \
  "gdb_port pipe;log_output openocd.log" --file ~/stm32l-discovery/openocd.cfg
set remote hardware-breakpoint-limit 6
set remote hardware-watchpoint-limit 4
