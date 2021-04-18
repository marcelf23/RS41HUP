#!/usr/bin/env bash
#
# OpenOCD Flash script, using commands from openocd_rs41.cfg
#

openocd -f ./openocd_rs41.cfg -c "init; halt; flash protect 0 0 30 off; exit"

openocd -f ./openocd_rs41.cfg -c "program RS41HUP.elf verify reset exit"

openocd -f ./openocd_rs41.cfg -c "init; reset; exit"