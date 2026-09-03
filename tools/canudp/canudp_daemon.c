/*
 * canudp_daemon - bridges a local SocketCAN interface (typically a vcan)
 * to a remote UDP-based CAN device, e.g. a DIY board with CAN FD +
 * Ethernet acting as its own USB-CAN-style adapter but over the network.
 *
 * Handles both classic CAN and CAN FD frames (up to 8 data bytes, per
 * the current wire protocol - see canudp_proto.h).
 *
 * Usage:
 *   canudp_daemon -i vcan0 -l 21000 -r 192.168.1.50 -R 21000
 *
 *   -i  local SocketCAN interface (must already exist and have FD MTU
 *       set, see setup_vcan.sh)
 *   -l  local UDP port to listen on (frames FROM the board arrive here)
 *   -r  remote board IP (frames TO the board are sent here)
 *   -R  remote board UDP port
 */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "canudp_proto.h"

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static int open_can(const char *ifname) {
  int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (s < 0) {
    perror("socket(PF_CAN)");
    return -1;
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
  if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
    perror("ioctl(SIOCGIFINDEX)");
    close(s);
    return -1;
  }

  /* Allow both reading and writing CAN FD frames. If the interface
   * MTU wasn't raised to CANFD_MTU, FD frames just won't fit and the
   * kernel will reject the write - classic frames still work fine. */
  int enable_fd = 1;
  if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd,
                 sizeof(enable_fd)) < 0) {
    perror("setsockopt(CAN_RAW_FD_FRAMES) - continuing, classic frames only");
  }

  struct sockaddr_can addr;
  memset(&addr, 0, sizeof(addr));
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind(CAN)");
    close(s);
    return -1;
  }
  return s;
}

static int open_udp(uint16_t local_port) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    perror("socket(UDP)");
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(local_port);

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind(UDP)");
    close(s);
    return -1;
  }
  return s;
}

/* Extract common fields (id/flags/len/data) from a just-read CAN socket
 * buffer of nbytes, which is either a classic can_frame (CAN_MTU) or a
 * canfd_frame (CANFD_MTU) - SocketCAN distinguishes them purely by the
 * size of the read, not an in-struct flag. Builds the wire frame. */
static int can_to_wire(const uint8_t *raw, ssize_t nbytes,
                       struct canudp_frame *w) {
  uint32_t id;
  uint8_t top_flags = 0;
  uint8_t fd_flags = 0;
  uint8_t len;
  const uint8_t *data;

  if (nbytes == CAN_MTU) {
    const struct can_frame *cf = (const struct can_frame *)raw;
    id = cf->can_id;
    len = cf->can_dlc;
    data = cf->data;
  } else if (nbytes == CANFD_MTU) {
    const struct canfd_frame *cfd = (const struct canfd_frame *)raw;
    id = cfd->can_id;
    len = cfd->len;
    data = cfd->data;
    fd_flags |= CANUDP_FDFLAG_FDF;
    if (cfd->flags & CANFD_BRS)
      fd_flags |= CANUDP_FDFLAG_BRS;
    if (cfd->flags & CANFD_ESI)
      fd_flags |= CANUDP_FDFLAG_ESI;
  } else {
    return -1; /* unexpected read size, ignore */
  }

  if (id & CAN_EFF_FLAG) {
    top_flags |= CANUDP_FLAG_EFF;
    id &= CAN_EFF_MASK;
  } else {
    id &= CAN_SFF_MASK;
  }
  if (id & CAN_RTR_FLAG)
    top_flags |= CANUDP_FLAG_RTR; /* harmless if unset */

  if (len > CANUDP_DATA_LEN) {
    fprintf(stderr,
            "canudp: truncating %u-byte frame to %u "
            "(current protocol only carries %u data bytes)\n",
            len, CANUDP_DATA_LEN, CANUDP_DATA_LEN);
    len = CANUDP_DATA_LEN;
  }

  memset(w, 0, sizeof(*w));
  w->version = CANUDP_VERSION;
  w->flags = top_flags;
  canudp_pack_id(w->can_id, id);
  w->len = len;
  w->fd_flags = fd_flags;
  memcpy(w->data, data, len);
  return 0;
}

/* wire format -> classic can_frame or canfd_frame, written directly into
 * outbuf (must be at least CANFD_MTU bytes). Returns the size to write()
 * to the CAN socket (CAN_MTU or CANFD_MTU), or -1 on malformed input. */
static ssize_t wire_to_can(const uint8_t *buf, size_t len, uint8_t *outbuf) {
  if (len != CANUDP_FRAME_SIZE)
    return -1;

  struct canudp_frame w;
  memcpy(&w, buf, sizeof(w));

  if (w.version != CANUDP_VERSION)
    return -1;
  if (w.len > CANUDP_DATA_LEN)
    return -1;

  uint32_t id = canudp_unpack_id(w.can_id);
  canid_t can_id = (w.flags & CANUDP_FLAG_EFF)
                       ? ((id & CAN_EFF_MASK) | CAN_EFF_FLAG)
                       : (id & CAN_SFF_MASK);
  if (w.flags & CANUDP_FLAG_RTR)
    can_id |= CAN_RTR_FLAG;

  if (w.fd_flags == 0) {
    struct can_frame *cf = (struct can_frame *)outbuf;
    memset(cf, 0, sizeof(*cf));
    cf->can_id = can_id;
    cf->can_dlc = w.len;
    memcpy(cf->data, w.data, w.len);
    return CAN_MTU;
  } else {
    struct canfd_frame *cfd = (struct canfd_frame *)outbuf;
    memset(cfd, 0, sizeof(*cfd));
    cfd->can_id = can_id;
    cfd->len = w.len;
    if (w.fd_flags & CANUDP_FDFLAG_BRS)
      cfd->flags |= CANFD_BRS;
    if (w.fd_flags & CANUDP_FDFLAG_ESI)
      cfd->flags |= CANFD_ESI;
    memcpy(cfd->data, w.data, w.len);
    return CANFD_MTU;
  }
}

int main(int argc, char **argv) {
  const char *ifname = "vcan0";
  const char *remote_ip = NULL;
  uint16_t local_port = 0, remote_port = 0;

  int opt;
  while ((opt = getopt(argc, argv, "i:l:r:R:")) != -1) {
    switch (opt) {
    case 'i':
      ifname = optarg;
      break;
    case 'l':
      local_port = (uint16_t)atoi(optarg);
      break;
    case 'r':
      remote_ip = optarg;
      break;
    case 'R':
      remote_port = (uint16_t)atoi(optarg);
      break;
    default:
      fprintf(
          stderr,
          "Usage: %s -i vcan0 -l <local_port> -r <board_ip> -R <board_port>\n",
          argv[0]);
      return 1;
    }
  }
  if (!remote_ip || !local_port || !remote_port) {
    fprintf(
        stderr,
        "Usage: %s -i vcan0 -l <local_port> -r <board_ip> -R <board_port>\n",
        argv[0]);
    return 1;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  int can_sock = open_can(ifname);
  if (can_sock < 0)
    return 1;

  int udp_sock = open_udp(local_port);
  if (udp_sock < 0) {
    close(can_sock);
    return 1;
  }

  struct sockaddr_in remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));
  remote_addr.sin_family = AF_INET;
  remote_addr.sin_port = htons(remote_port);
  if (inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr) != 1) {
    fprintf(stderr, "bad remote IP: %s\n", remote_ip);
    return 1;
  }

  fprintf(stderr, "canudp: %s <-> %s:%u (listening on :%u), FD-capable\n",
          ifname, remote_ip, remote_port, local_port);

  struct pollfd fds[2] = {
      {.fd = can_sock, .events = POLLIN, .revents = 0},
      {.fd = udp_sock, .events = POLLIN, .revents = 0},
  };

  while (!g_stop) {
    int n = poll(fds, 2, 500);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("poll");
      break;
    }

    /* CAN -> UDP (board-bound) */
    if (fds[0].revents & POLLIN) {
      uint8_t raw[CANFD_MTU];
      ssize_t r = read(can_sock, raw, sizeof(raw));
      if (r > 0) {
        struct canudp_frame w;
        if (can_to_wire(raw, r, &w) == 0) {
          sendto(udp_sock, &w, sizeof(w), 0, (struct sockaddr *)&remote_addr,
                 sizeof(remote_addr));
        }
      }
    }

    /* UDP -> CAN (from board) */
    if (fds[1].revents & POLLIN) {
      uint8_t buf[64];
      struct sockaddr_in src;
      socklen_t slen = sizeof(src);
      ssize_t r = recvfrom(udp_sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&src, &slen);
      if (r > 0) {
        uint8_t outbuf[CANFD_MTU];
        ssize_t wsize = wire_to_can(buf, (size_t)r, outbuf);
        if (wsize > 0) {
          if (write(can_sock, outbuf, (size_t)wsize) < 0) {
            perror("write(CAN)");
          }
        } else {
          fprintf(stderr, "canudp: dropped malformed packet (%zd bytes)\n", r);
        }
      }
    }
  }

  fprintf(stderr, "canudp: shutting down\n");
  close(can_sock);
  close(udp_sock);
  return 0;
}
