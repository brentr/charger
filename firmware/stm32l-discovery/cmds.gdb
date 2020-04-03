define armex
  printf "EXEC_RETURN (LR):\n",
  info registers $lr
    if $lr & 0x4 == 0x4
      printf "Uses MSP 0x%x return.\n", $MSP
      set $armex_base = $MSP
    else
      printf "Uses PSP 0x%x return.\n", $PSP
      set $armex_base = $PSP
    end

    printf "xPSR            0x%x\n", *($armex_base+28)
    printf "ReturnAddress   0x%x\n", *($armex_base+24)
    printf "LR (R14)        0x%x\n", *($armex_base+20)
    printf "R12             0x%x\n", *($armex_base+16)
    printf "R3              0x%x\n", *($armex_base+12)
    printf "R2              0x%x\n", *($armex_base+8)
    printf "R1              0x%x\n", *($armex_base+4)
    printf "R0              0x%x\n", *($armex_base)
    printf "Return instruction:\n"
    x/i *($armex_base+24)
    printf "LR instruction:\n"
    x/i *($armex_base+20)
end

document armex
ARMv7 Exception entry behavior.
xPSR, ReturnAddress, LR (R14), R12, R3, R2, R1, and R0
end

define reload
  monitor reset halt
  load
end
define remake
  make
  reload
end
define reset
  monitor reset halt
  continue
end

#the methods below facilitate starting "booted" images in bootloader's absence
define reset2PSP
#reset and switch to process stack
  monitor reset halt
#  set $psp=(void *)&__process_stack_end__
  set $control=2
end
define restart
  reset2PSP
  jump restart
end
define reboot
  reset2PSP
  jump reboot
end
define bootloader
  reset2PSP
  jump bootloader
end
define start
  tbreak main
  reboot
end
