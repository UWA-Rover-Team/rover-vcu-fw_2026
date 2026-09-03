/*
 * UDP transport layer, bound to CONFIG_GATEWAY_UDP_PORT (5555 by default).
 * Carries two independent, unrelated wire formats over the same socket:
 *
 *  1. CAN-over-UDP passthrough (struct canudp_frame, see canudp_proto.h).
 *     Fixed 25-byte datagrams with no framing/type byte of their own -
 *     any datagram of exactly CANUDP_FRAME_SIZE is treated as one of
 *     these. This is what the host-side canudp_daemon "driver shim"
 *     (tools/canudp/canudp_daemon.c) speaks: it exposes a virtual
 *     SocketCAN interface (vcan) to can-utils tools (cansend, candump,
 *     etc.) and bridges raw CAN traffic to/from the board over UDP.
 *       - device -> host: can_sniff_thread() forwards every received CAN
 *         frame as a canudp_frame broadcast to CONFIG_GATEWAY_UDP_PORT.
 *       - host -> device: udp_rx_thread() parses inbound datagrams with
 *         udp_parse_frame() and injects the decoded frame onto the CAN
 *         bus via can_send().
 *
 *  2. Application command/telemetry protocol (struct UDPPacketHeader +
 *     enum PacketType below). A small type/version/len header followed
 *     by one of the payload structs in app_types.h (drive/arm/safety
 *     commands inbound, telemetry outbound via struct UDPReceivedPacket
 *     / struct UDPTransmittedPacket). Parsed by udp_transport_rx_parse().
 *
 * The two formats are disambiguated purely by datagram length (exactly
 * CANUDP_FRAME_SIZE selects protocol 1); there is no protocol-id byte.
 */

#pragma once
#include "app_types.h"
#include "canudp_proto.h"
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#define UDP_PACKET_SIZE 1024
#define UDP_THREAD_STACK_SIZE 2048
#define UDP_TRANSPORT_THREAD_PRIORITY 5

#define CAN_SNIFF_MSGQ_DEPTH 64
#define CAN_SNIFF_STACK_SIZE 2048
#define CAN_SNIFF_THREAD_PRIORITY 5

/* Header for protocol 2 (app command/telemetry). `len` is the payload
 * size in bytes that follows this header and must match sizeof() of
 * the struct selected by `type` (see enum PacketType). */
struct UDPPacketHeader {
  uint8_t type;
  uint8_t version;
  uint16_t len;
};

enum PacketType {
  // transmit
  PKT_RX_DRIVE_VEL_CMD = 1,
  PKT_RX_DRIVE_POS_CMD = 2,
  PKT_RX_ARM_CMD = 3,
  PKT_RX_SW_SAFETY_CMD = 4,

  // receive
  PKT_TX_IMU_TELEMETRY = 11,
  PKT_TX_POWER_TELEMETRY = 12,
  PKT_TX_MOTOR_STATES = 13,
  PKT_TX_ARM_MOTOR_STATES = 14,
  PKT_TX_PERIPHERAL_TELEMETRY = 15,
  PKT_TX_SAFETY_STATE = 16,
};

struct UDPTransport {
  int udp_sock;
  // msgq for received packets to main control loop
  struct k_msgq rx_msgq;
  struct k_msgq tx_msgq;
  struct UDPReceivedPacket *rx_msgq_buffer[100];
  struct UDPTransmittedPacket *tx_msgq_buffer[100];

  struct k_msgq can_rx_msgq;
  struct can_frame can_rx_msgq_buffer[CAN_SNIFF_MSGQ_DEPTH];
};

struct UDPReceivedPacket {
  enum PacketType type;
  union {
    struct DriveVelocityCommand drive_vel_cmd;
    struct DrivePositionCommand drive_pos_cmd;
    struct ArmCommand arm_cmd;
    struct SWSafetyCommands sw_safety_cmd;
  };
};

struct UDPTransmittedPacket {
  enum PacketType type;
  union {
    struct IMUTelemetry imu_telemetry;
    struct PowerTelemetry power_telemetry;
    struct MotorStates motor_states;
    struct ArmMotorStates arm_motor_states;
    struct PeripheralTelemetry peripheral_telemetry;
    struct SafetyState safety_state;
  };
};

int udp_transport_init(struct UDPTransport *udp_transport);
struct UDPReceivedPacket
udp_transport_rx_parse(struct UDPTransport *udp_transport, uint8_t *buf,
                       ssize_t len);
void udp_rx_thread(void *p1, void *p2, void *p3);
struct UDPReceivedPacket
udp_transport_rx_parse(struct UDPTransport *udp_transport, uint8_t *buf,
                       ssize_t len);
void can_sniff_thread(void *p1, void *p2, void *p3);
