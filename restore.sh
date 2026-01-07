#!/bin/bash

IFACE="lo"

sudo tc qdisc del dev $IFACE root 2>/dev/null
#comando para que tc no modifique lo enviado