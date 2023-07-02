# Names: Stanley Vossler, Carlos Pantoja-Malaga, Matthew Echenique
# Professor: Andy Wang, PhD
# Class: COP 4610
# Project: 2
# Description: This is the makefile for 3rd part of Project 3: "Kernel Module Programming"

obj-y := sys_call.o
obj-m := barstool.o

PWD := $(shell pwd)
KDIR := /lib/modules/`uname -r`/build

default:
        $(MAKE) -C $(KDIR) M=$(PWD) SUBDIRS=$(PWD) modules

clean:
        rm -f *.o *.ko *.mod.* Module.* modules.* *.mod .*.cmd
