#!/bin/bash
#sudo rmmod flashix
sudo insmod reflex.ko
#sudo mke2fs -b 4096 -t ext4 /dev/flashix0
sudo mount -t ext4 /dev/reflex0 /mnt/reflex
sudo chmod a+rwx /mnt/reflex
#touch /mnt/flashix/fio
