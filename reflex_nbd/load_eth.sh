#!/bin/bash
sudo ifconfig enp6s0f1 up
sudo ifconfig enp6s0f1 10.79.6.131/24
sudo ethtool -K enp6s0f1 lro off
sudo ethtool -K enp6s0f1 gro off
sudo ethtool -C enp6s0f1 rx-usecs 10
