## 0.1.0 (2026-09-03)

### Feat

- added UDP Transport drivers and configs
- added can sniffing
- implemneted udp can transmissions
- added telnet shell for debugging
- added mcuboot for remote dfu via udp
- adding mcuboot
- added vcu board files
- added fsm thread for running hte fsm functions;
- added udp pkt handling and msg enqueue for rx path
- added UDP receive thread
- added ports to fw for IPC comms
- implementing state handlers and port definitions for domain logic

### Fix

- highlighting printout and version
- udp receive now working
- static ip setting for testing
- deprication notice about timer config
- added static IP assignent after 10s
- added led blinky
- pin configs for bringup
- cleaning up fsm flow
- changed repo git link for getting started

### Refactor

- move dhcp init and ip request to separate file for main clarity
- maove fsm commit state to end lol

## 0.0.3 (2026-06-14)

### Fix

- test build

## 0.0.2 (2026-06-13)

### Fix

- main int output

## 0.0.1 (2026-06-13)
