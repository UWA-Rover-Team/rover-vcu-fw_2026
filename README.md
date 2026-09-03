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

```ini
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

## Firmware Update with MCU Boot
The firmware is built with [MCUboot](https://docs.mcuboot.com/) as the bootloader
(via Zephyr sysbuild, see `app/sysbuild.conf`), and uses MCUmgr's UDP transport
for over-the-network DFU. A build produces a signed image at
`build/app/zephyr/zephyr.signed.bin` which can be uploaded to a device already
running MCUboot without a debug probe.

### Prerequesite Installs
`mcumgr` is distributed as a Go module, so you'll need a Go toolchain installed
first.

```sh
# Debian/Ubuntu
sudo apt update
sudo apt install golang-go

# macOS
brew install go
```

Verify the install and check `GOPATH` (where `go install` places binaries):

```sh
go version
go env GOPATH
```

Add `$GOPATH/bin` (typically `~/go/bin`) to your `PATH` if it isn't already,
e.g. by adding this to your `~/.bashrc`, `~/.zshrc`, etc:

```sh
export PATH="$PATH:$(go env GOPATH)/bin"
```

Now install the `mcumgr` CLI tool (used to upload images and manage the
bootloader over UDP):

```sh
go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest
```

Confirm it's on your `PATH`:

```sh
mcumgr version
```

### Uploading a `.bin` Image
The device listens for MCUmgr SMP requests on UDP port `1337`
(`CONFIG_MCUMGR_TRANSPORT_UDP_PORT` in `app/prj.conf`). Replace `<DEVICE_IP>`
with the device's IP address.

```sh
# upload the signed image to the device's inactive slot
mcumgr --conntype udp --connstring "[<DEVICE_IP>]:1337" image upload build/app/zephyr/zephyr.signed.bin

# confirm the new image landed in slot 1 and note its hash
mcumgr --conntype udp --connstring "[<DEVICE_IP>]:1337" image list

# mark the new image for a one-time test boot
mcumgr --conntype udp --connstring "[<DEVICE_IP>]:1337" image test <IMAGE_HASH>

# reboot the device into the new image
mcumgr --conntype udp --connstring "[<DEVICE_IP>]:1337" reset
```

After confirming the new firmware boots and runs correctly, permanently confirm
it (otherwise MCUboot will revert to the previous image on the next reboot):

```sh
mcumgr --conntype udp --connstring "[<DEVICE_IP>]:1337" image confirm <IMAGE_HASH>
```

## `telnet` for debugging
Debug builds expose a Zephyr shell over telnet instead of (or in addition to)
the serial console, which is useful when the device is only reachable over
the network. This is controlled by `app/debug.conf`
(`CONFIG_SHELL_BACKEND_TELNET=y`), which is not enabled in a normal release
build.

### Building with the shell enabled
Add `debug.conf` as an extra Kconfig fragment when configuring the build:

```sh
west build -b rover_vcu_v0 -p always --sysbuild app -- -DEXTRA_CONF_FILE=debug.conf
```

### Connecting
The telnet shell listens on TCP port `23` by default
(`CONFIG_SHELL_BACKEND_TELNET_PORT`). Connect with `telnet` once the device
has an IP address (check your DHCP leases, or see `CONFIG_STATIC_VCU_DEV` in
`app/debug.conf` for a static-IP debug config):

```sh
telnet <DEVICE_IP>
```

Once connected you get an interactive Zephyr shell (`kernel`, `log`, `net`,
etc. subcommands, plus any app-specific shell commands). Only one telnet
client can be attached at a time. 

## Editing Firmware
This project uses zephyr. You'll need to clone down the repo using west. 
Follow the zephyr install instructions then run the following commands to initialize the west workspace:

```bash
west init -m <REPO_URL> --mr main <WORKSPACE_DIR>
cd <WORKSPACE_DIR>
west update
``` 

This may take a few minutes depending on your internet connection and the size of the repository.

To build and flash the firmware, run the following commands:

```bash
cd <WORKSPACE_DIR>
cd rover-vcu-fw_2026
west build -b rover_vcu_v0 -p always --sysbuild app -- -DEXTRA_CONF_FILE=debug.conf
west flash
```