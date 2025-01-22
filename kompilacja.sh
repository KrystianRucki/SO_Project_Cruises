#!/bin/bash
gcc scheduler.c -o scheduler
gcc sternik.c -o sternik
gcc cashier.c -o cashier
gcc passenger.c -o passenger
gcc police.c -o police
#app should be started by executing ./scheduler