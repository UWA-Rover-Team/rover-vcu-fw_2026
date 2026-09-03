#!/bin/sh
# One-time setup: create and bring up a virtual CAN interface with FD support.
# Usage: ./setup_vcan.sh [ifname]   (defaults to vcan0)
#
# NOTE: vcan defaults to MTU 16 (CAN_MTU, classic-only). CAN FD frames are
# transported as CANFD_MTU (72) byte structures, so the interface MTU must
# be raised or the kernel will reject FD frames on write.
set -e
IFACE=${1:-vcan0}

sudo modprobe vcan
sudo ip link add dev "$IFACE" type vcan 2>/dev/null || true
sudo ip link set "$IFACE" mtu 72   # CANFD_MTU - enables FD frame support
sudo ip link set up "$IFACE"

echo "Interface $IFACE is up with FD support. Verify with: ip -d link show $IFAC"
