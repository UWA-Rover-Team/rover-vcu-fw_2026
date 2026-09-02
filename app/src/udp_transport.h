#pragma once
#include "app_types.h"
#include "canudp_proto.h"
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#define UDP_PORT 5559
#define UDP_PACKET_SIZE 1024
#define UDP_THREAD_STACK_SIZE 2048
#define UDP_TRANSPORT_THREAD_PRIORITY 5

#define CAN_SNIFF_MSGQ_DEPTH 64
#define CAN_SNIFF_STACK_SIZE 2048
#define CAN_SNIFF_THREAD_PRIORITY 5

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