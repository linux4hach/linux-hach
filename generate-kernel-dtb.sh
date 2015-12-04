#! /bin/bash
set ARCH=arm
set CROSS_COMPILE=/opt/Programs/buildroot-at91/output/host/usr/bin/arm-linux-
make hach-at91sam9g35_defconfig
#make kernel
make
#make dtb
make at91sam9g35ek.dtb
