#!/bin/bash

IFACE="enx00e04c68cf22"

sudo tc qdisc del dev $IFACE root 2>/dev/null
#comando para que tc no modifique lo enviado