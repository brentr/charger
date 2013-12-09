define reload
  monitor reset halt
  load
  tbreak main
  continue
end
define remake
  make
  reload
end
source ~/stm32l-discovery/target.gdb
load
monitor reset halt
tbreak main
continue
