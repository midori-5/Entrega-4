#!/bin/bash
#script para "corromper" los paquetes de salida
IFACE="lo"
PORT=4950
PERC=0.3
# A partir del inicio del encabezado IP (para modificar el 1er byte de la carga util sería el byte #28)
BYTE=100
ACTION="invert" # "set FF"

# 1) Marcar aleatoriamente ~$PERC% de los UDP hacia puerto $PORT por la interfaz $IFACE
sudo iptables -t mangle -F
sudo iptables -t mangle -A OUTPUT -o $IFACE -p udp --sport $PORT \
  -m statistic --mode random --probability $PERC \
  -j MARK --set-mark 0x1

# 2) Qdisc
sudo tc qdisc del dev $IFACE root 2>/dev/null
sudo tc qdisc add dev $IFACE root handle 1: prio

# Solo paquetes con mark 0x1 (los del $PERC%), y voltea el byte en offset $BYTE
sudo tc filter add dev $IFACE parent 1:0 protocol ip prio 1 \
  handle 1 fw \
  action pedit munge offset $BYTE u8 $ACTION \
  action ok