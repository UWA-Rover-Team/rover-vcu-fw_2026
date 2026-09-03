#include "udp_transport.h"
#include "syscalls/device.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(udp_transport);

const struct device *can_dev_0 = DEVICE_DT_GET(DT_NODELABEL(fdcan1));

K_THREAD_STACK_DEFINE(udp_rx_stack, UDP_THREAD_STACK_SIZE);
static struct k_thread udp_rx_thread_data;

K_THREAD_STACK_DEFINE(can_sniff_stack, CAN_SNIFF_STACK_SIZE);
static struct k_thread can_sniff_thread_data;

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
  bind_addr.sin_port = htons(CONFIG_GATEWAY_UDP_PORT);
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

  int broadcast_enable = 1;
  ret = zsock_setsockopt(udp_transport->udp_sock, SOL_SOCKET, SO_BROADCAST,
                         &broadcast_enable, sizeof(broadcast_enable));
  if (ret < 0) {
    LOG_WRN("SO_BROADCAST not available on this socket (%d) - continuing, "
            "broadcast sendto() should still work",
            errno);
  }

  // CAN BUS STARTUPS
  if (!device_is_ready(can_dev_0)) {
    LOG_ERR("Failed to start Telemetry CAN Bus");
    return 1;
  }

  can_set_mode(can_dev_0, CAN_MODE_NORMAL);

  struct can_timing timing;

  ret = can_calc_timing(can_dev_0, &timing, 250000, 875);
  if (ret > 0) {
    LOG_INF("Sample-Point error: %d", ret);
  }

  ret = can_set_timing(can_dev_0, &timing);
  if (ret != 0) {
    LOG_ERR("Failed to set timing");
  }

  ret = can_start(can_dev_0);
  if (ret < 0) {
    LOG_ERR("Failed to start Telemetry CAN Bus");
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

  k_msgq_init(&udp_transport->can_rx_msgq,
              (char *)udp_transport->can_rx_msgq_buffer,
              sizeof(struct can_frame), CAN_SNIFF_MSGQ_DEPTH);

  const struct can_filter std_filter = {
      .id = 0,
      .mask = 0,
      .flags = 0,
  };

  const struct can_filter ext_filter = {
      .id = 0,
      .mask = 0,
      .flags = CAN_FILTER_IDE,
  };

  ret = can_add_rx_filter_msgq(can_dev_0, &udp_transport->can_rx_msgq,
                               &std_filter);
  if (ret < 0) {
    LOG_ERR("Failed to add CAN std-id sniff filter: %d", ret);
  }

  ret = can_add_rx_filter_msgq(can_dev_0, &udp_transport->can_rx_msgq,
                               &ext_filter);
  if (ret < 0) {
    LOG_ERR("Failed to add CAN ext-id sniff filter: %d", ret);
  }

  k_thread_create(&can_sniff_thread_data, can_sniff_stack,
                  K_THREAD_STACK_SIZEOF(can_sniff_stack), can_sniff_thread,
                  udp_transport, NULL, NULL, CAN_SNIFF_THREAD_PRIORITY, 0,
                  K_NO_WAIT);

  LOG_INF("Socket ready...");

  return 0;
}

int udp_parse_frame(uint8_t *rx_buf, int len, struct can_frame *can_frame) {

  if (len != (int)CANUDP_FRAME_SIZE) {
    LOG_ERR("Unexpected packet size %d (expected %d)", len,
            (int)CANUDP_FRAME_SIZE);
    return -1;
  }

  struct canudp_frame w;
  memcpy(&w, rx_buf, sizeof(w));

  if (w.version != CANUDP_VERSION) {
    LOG_ERR("Unexpected protocol version %x", w.version);
    return -1;
  }

  uint32_t can_id = canudp_unpack_id(w.can_id);
  uint8_t can_flags = 0;

  if (w.flags & CANUDP_FLAG_EFF) {
    can_id &= CAN_EXT_ID_MASK;
    can_flags |= CAN_FRAME_IDE;
  } else {
    can_id &= CAN_STD_ID_MASK;
  }
  if (w.flags & CANUDP_FLAG_RTR) {
    can_flags |= CAN_FRAME_RTR;
  }

  uint8_t dlc = w.len;
  if (dlc > CANUDP_DATA_LEN) {
    dlc = CANUDP_DATA_LEN;
    LOG_WRN("Cannot exceed 8 DLC - only sending 8 bytes and setting DLC to 8");
  }

  // TODO: extend to FD oneday; for now, we just care about classic, so
  // w.fd_flags is ignored here.
  memset(can_frame, 0, sizeof(*can_frame));
  can_frame->id = can_id;
  can_frame->dlc = dlc;
  can_frame->flags = can_flags;
  memcpy(can_frame->data, w.data, dlc);

  LOG_INF("Protocol Version %x", w.version);
  LOG_INF("Target CANID %x", can_id);
  LOG_INF("Target DLC %x", dlc);
  LOG_HEXDUMP_INF(can_frame->data, 8, "CAN Data");

  return 0;
}

void udp_rx_thread(void *p1, void *p2, void *p3) {
  int ret = 0;
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  // p1 is a udp_transport type; we get sock from there
  struct UDPTransport *udp_transport = (struct UDPTransport *)p1;

  LOG_WRN("UDP RX Thread started, listening on port %d",
          CONFIG_GATEWAY_UDP_PORT);

  for (;;) {
    uint8_t rx_buf[UDP_PACKET_SIZE] = {0};
    struct can_frame frame = {0};

    int pkt_size =
        zsock_recv(udp_transport->udp_sock, rx_buf, UDP_PACKET_SIZE, 0);
    if (pkt_size < 0) {
      LOG_ERR("Packet was a dud...");
      continue;
    }

    int parse_ret = udp_parse_frame(rx_buf, pkt_size, &frame);
    if (parse_ret < 0) {
      LOG_ERR("Failed to parse frame...");
      continue;
    }

    LOG_HEXDUMP_INF(rx_buf, pkt_size, "RX buffer");

    ret = can_send(can_dev_0, &frame, K_MSEC(100), NULL, NULL);

    if (ret < 0) {
      enum can_state state;
      struct can_bus_err_cnt err_cnt;

      if (can_get_state(can_dev_0, &state, &err_cnt) == 0) {
        LOG_ERR("can state: %d, tx_err_cnt: %d, rx_err_cnt: %d", state,
                err_cnt.tx_err_cnt, err_cnt.rx_err_cnt);
      }
    }
  }
}

void can_sniff_thread(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  struct UDPTransport *udp_transport = (struct UDPTransport *)p1;
  struct can_frame frame;

  struct sockaddr_in bcast_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(CONFIG_GATEWAY_UDP_PORT),
      .sin_addr.s_addr = htonl(INADDR_BROADCAST),
  };

  LOG_WRN("CAN sniff thread started, broadcasting on port %d",
          CONFIG_GATEWAY_UDP_PORT);

  for (;;) {
    k_msgq_get(&udp_transport->can_rx_msgq, &frame, K_FOREVER);

    struct canudp_frame w;
    memset(&w, 0, sizeof(w));
    w.version = CANUDP_VERSION;

    uint32_t id = frame.id;
    if (frame.flags & CAN_FRAME_IDE) {
      w.flags |= CANUDP_FLAG_EFF;
      id &= CAN_EXT_ID_MASK;
    } else {
      id &= CAN_STD_ID_MASK;
    }
    if (frame.flags & CAN_FRAME_RTR) {
      w.flags |= CANUDP_FLAG_RTR;
    }
    canudp_pack_id(w.can_id, id);

    // NOTE: classic CAN only for now - frame.dlc is already a byte
    // count (0-8) here. If FD support is added later this needs
    // can_dlc_to_bytes(frame.dlc), and w.fd_flags would need setting.
    uint8_t data_len = frame.dlc;
    if (data_len > CANUDP_DATA_LEN) {
      data_len = CANUDP_DATA_LEN;
    }
    w.len = data_len;
    w.fd_flags = 0;
    memcpy(w.data, frame.data, data_len);

    // this is a bare struct canudp_frame on the wire - same 25-byte
    // format canudp_daemon already speaks, no extra header
    int ret = zsock_sendto(udp_transport->udp_sock, &w, sizeof(w), 0,
                           (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
    if (ret < 0) {
      LOG_WRN("Failed to broadcast sniffed CAN frame: %d", errno);
    }
  }
}
