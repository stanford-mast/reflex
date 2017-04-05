#!/bin/bash

# Script to run ReFlex setup after machine reboot and start the ReFlex server

sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'
sudo modprobe -r ixgbe
sudo modprobe -r nvme
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko
sudo ./dp/ix -- ./apps/reflex_server
