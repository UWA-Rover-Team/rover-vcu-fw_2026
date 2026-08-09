#include "udp_transport.h"
#include "syscalls/device.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(udp_transport);

const struct device *can_dev_1 = DEVICE_DT_GET(DT_NODELABEL(fdcan1));
const struct device *can_dev_0 = DEVICE_DT_GET(DT_NODELABEL(fdcan2));

K_THREAD_STACK_DEFINE(udp_rx_stack, UDP_THREAD_STACK_SIZE);
static struct k_thread udp_rx_thread_data;

int udp_transport_init(struct UDPTransport *udp_transport) {

  LOG_INF("Initializing UDP Transport Layer");

  struct sockaddr_in bind_addr;
  int ret;

  udp_transport->udp_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_transport->udp_sock < 0) {
    LOG_ERR("Failed to create UDP Socket: %d", errno);
    return -1;
  }

  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(UDP_PORT);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  ret = zsock_bind(udp_transport->udp_sock, (struct sockaddr *)&bind_addr,
                   sizeof(bind_addr));
  if (ret < 0) {
    LOG_ERR("Failed to bind UDP Socket %d", errno);
    zsock_close(udp_transport->udp_sock);
    udp_transport->udp_sock = -1;
    return -errno;
  }

  LOG_INF("Socket seems to be ready...");

  // CAN BUS STARTUPS
  if (!device_is_ready(can_dev_0)) {
    LOG_ERR("Failed to start Telemetry CAN Bus");
    return 1;
  }

  if (!device_is_ready(can_dev_1)) {
    LOG_ERR("Failed to start Powertrain CAN Bus");
    return 1;
  }

  can_set_mode(can_dev_0, CAN_MODE_NORMAL);

  ret = can_start(can_dev_0);
  if (ret < 0) {
    LOG_ERR("Failed to start Telemetry CAN Bus");
    return -1;
  }

  can_set_mode(can_dev_1, CAN_MODE_NORMAL);
  ret = can_start(can_dev_1);
  if (ret < 0) {
    LOG_ERR("Failed to start Power Train CAN Bus");
    return -1;
  }

  // start the udp threads
  k_msgq_init(&udp_transport->rx_msgq, (char *)udp_transport->rx_msgq_buffer,
              sizeof(struct UDPReceivedPacket), 100);

  k_msgq_init(&udp_transport->tx_msgq, (char *)udp_transport->tx_msgq_buffer,
              sizeof(struct UDPTransmittedPacket), 100);

  k_thread_create(&udp_rx_thread_data, udp_rx_stack,
                  K_THREAD_STACK_SIZEOF(udp_rx_stack), udp_rx_thread,
                  udp_transport, NULL, NULL, UDP_TRANSPORT_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  LOG_INF("Socket ready...");

  return 0;
}

int udp_parse_frame(uint8_t *rx_buf, int len, struct can_frame *can_frame) {

  if (len < 0) {
    LOG_ERR("There must be bytes in the UDP Frame...");
    return -1;
  }

  /*
   * BYTE 0: Protocol Version
   * BYTE 1: Flags
   * BYTE 2-7: TODO: im sure theres some other tiestamp stuff we want later
   * BYTE 8-12: CAN ID
   * BYTE 13: data length
   * BYTE 14: flags; 0 for classic; fd uses it tho
   * BYTE 15-16: reserved; TODO: could use as timestamp later
   * BYTE 17-24: 8byte data output; the meat of things...
   * */

  // TODO: extend to FD oneday; for now, we just care about classic...
  uint8_t protocol_ver = rx_buf[0];
  uint8_t phy_channel = rx_buf[2];
  uint16_t can_id = (rx_buf[8] << 8 | rx_buf[9]) & 0b11111111111;
  uint8_t dlc = rx_buf[13];
  if (dlc > 8) {
    dlc = 8;
    LOG_WRN("Cannot exceed 8 DLC - only sending 8 bytes and setting DLC to 8");
  }
  uint8_t data[8] = {0};
  memcpy(&data, &rx_buf[9], dlc > 8 ? 8 : dlc);

  can_frame->dlc = dlc;
  can_frame->id = can_id;
  can_frame->flags = 0;
  memcpy(can_frame->data, data, 8);

  LOG_INF("Protocol Version %x", protocol_ver);
  LOG_INF("Physical CAN Channel %x", phy_channel);
  LOG_INF("Target CANID %3x", can_id);
  LOG_INF("Target DLC %x", dlc);
  LOG_HEXDUMP_INF(data, 8, "CAN Data");

  // TODO: fix this; its a nice work around for now; but want something
  // a bit easier to audit in the future
  if (phy_channel != 0) {
    // TODO: also dont like the idea of not 0 => 1
    return 1;
  } else {
    return 0;
  }

  return 0;
}

void udp_rx_thread(void *p1, void *p2, void *p3) {
  int ret = 0;
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  // p1 is a udp_transport type; we get sock from there
  struct UDPTransport *udp_transport = (struct UDPTransport *)p1;

  LOG_INF("UDP RX Thread started, listenign on port %d", UDP_PORT);

  for (;;) {
    uint8_t rx_buf[UDP_PACKET_SIZE] = {0};
    struct can_frame frame = {0};
    int pkt_size =
        zsock_recv(udp_transport->udp_sock, rx_buf, UDP_PACKET_SIZE, 0);
    if (pkt_size < 0) {
      LOG_ERR("Packet was a dud...");
    }

    int phy_channel = udp_parse_frame(rx_buf, pkt_size, &frame);
    if (phy_channel < 0) {
      LOG_ERR("Failed to parse frame...");
      continue;
    }

    ret = can_send(can_dev_0, &frame, K_MSEC(100), NULL, NULL);

    if (ret < 0) {
      LOG_ERR("Failed to can_send on ch: %d (%d)", phy_channel, ret);
    }

    LOG_HEXDUMP_INF(rx_buf, pkt_size, "RX buffer");
  }
}
