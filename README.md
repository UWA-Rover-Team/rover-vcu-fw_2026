# Rover UDP CAN Gateway
This project acts as the CAN to UDP gateway to enable control of the rovers CAN subsystems remotely from the basestation.

## Basestation Setup
### Prerequesites Packages
```
sudo apt update
sudo apt install can-utils
```

### `systemctl` Configuration
You need to daemonize the can-utils shim to create an interface. This allows you to use the 
prebuilt `cansniff`, `cangen`, `cansend`  tools for debugging. Make and install the binary 
in `tools/canudp`.

```sh
cd ./tools/canudp/
setup_vcan.sh vcan0
make
sudo install -Dm755 ./tools/canudp/canudp_daemon /usr/local/bin/canudp_daemon
sudo install -Dm755 ./tools/canudp/setup_vcan.sh /usr/local/bin/setup_vcan.sh
```

### `systemctl` Service File Setup

Edit the `.service` file in `./tools`. Change `<GATEWAY_IP>` to the IP of the device. 
This will default to `192.168.8.158` if no DHCP server is present. Once you've edited 
it, copy the file to `/etc/systemd/system/canudp.service`. You may need `sudo`
privliges. 

```toml
[Unit]
Description=CAN-over-UDP bridge daemon (vcan0)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStartPre=/usr/local/bin/setup_vcan.sh vcan0
ExecStart=/usr/local/bin/canudp_daemon -i vcan0 -l 5555 -r <GATEWAY_IP> -R 5555
Restart=on-failure
RestartSec=2
User=root

[Install]
WantedBy=multi-user.target
```

### Running `can-utils` tools with `vcan0`
```bash
# generate random CAN traffic on vcan0
cangen vcan0

# watch traffic on vcan0
candump vcan0

# generate CAN FD frames specifically (since your setup raised MTU to 72 for FD)
cangen vcan0 -f

# send a single frame manually
cansend vcan0 123#DEADBEEF
```
