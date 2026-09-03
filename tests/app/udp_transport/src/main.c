#include "app_types.h"
#include "udp_transport.h"
#include <string.h>
#include <zephyr/ztest.h>

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static size_t build_packet(uint8_t *buf, uint8_t type, const void *payload,
                           size_t payload_len) {
  struct UDPPacketHeader hdr = {
      .type = type,
      .version = 1,
      .len = (uint16_t)payload_len,
  };
  memcpy(buf, &hdr, sizeof(hdr));
  if (payload && payload_len) {
    memcpy(buf + sizeof(hdr), payload, payload_len);
  }
  return sizeof(hdr) + payload_len;
}

/* --------------------------------------------------------------------------
 * Happy-path tests
 * -------------------------------------------------------------------------- */

ZTEST(udp_transport_parse, test_parse_drive_vel_cmd) {
  uint8_t buf[UDP_PACKET_SIZE];

  struct DriveVelocityCommand cmd = {
      .hdr = {.cmdType = 1, .seqNum = 10, .timestampMs = 500},
      .velocity = 1.5f,
      .headingRad = 0.78f,
  };
  size_t pkt_len =
      build_packet(buf, PKT_RX_DRIVE_VEL_CMD, &cmd, sizeof(cmd));

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, (ssize_t)pkt_len);

  zassert_equal(pkt.type, PKT_RX_DRIVE_VEL_CMD, "wrong packet type");
  zassert_within(pkt.drive_vel_cmd.velocity, 1.5f, 0.001f,
                 "velocity mismatch");
  zassert_within(pkt.drive_vel_cmd.headingRad, 0.78f, 0.001f,
                 "headingRad mismatch");
  zassert_equal(pkt.drive_vel_cmd.hdr.seqNum, 10, "seqNum mismatch");
}

ZTEST(udp_transport_parse, test_parse_drive_pos_cmd) {
  uint8_t buf[UDP_PACKET_SIZE];

  struct DrivePositionCommand cmd = {
      .hdr = {.cmdType = 2, .seqNum = 20, .timestampMs = 1000},
      .distance = 3.0f,
      .headingRad = -0.5f,
  };
  size_t pkt_len =
      build_packet(buf, PKT_RX_DRIVE_POS_CMD, &cmd, sizeof(cmd));

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, (ssize_t)pkt_len);

  zassert_equal(pkt.type, PKT_RX_DRIVE_POS_CMD, "wrong packet type");
  zassert_within(pkt.drive_pos_cmd.distance, 3.0f, 0.001f,
                 "distance mismatch");
  zassert_within(pkt.drive_pos_cmd.headingRad, -0.5f, 0.001f,
                 "headingRad mismatch");
  zassert_equal(pkt.drive_pos_cmd.hdr.seqNum, 20, "seqNum mismatch");
}

ZTEST(udp_transport_parse, test_parse_arm_cmd) {
  uint8_t buf[UDP_PACKET_SIZE];

  struct ArmCommand cmd = {
      .hdr = {.cmdType = 3, .seqNum = 30, .timestampMs = 2000},
      .jointIdx = 2,
      .armAngleRad = 1.2f,
  };
  size_t pkt_len = build_packet(buf, PKT_RX_ARM_CMD, &cmd, sizeof(cmd));

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, (ssize_t)pkt_len);

  zassert_equal(pkt.type, PKT_RX_ARM_CMD, "wrong packet type");
  zassert_equal(pkt.arm_cmd.jointIdx, 2, "jointIdx mismatch");
  zassert_within(pkt.arm_cmd.armAngleRad, 1.2f, 0.001f,
                 "armAngleRad mismatch");
  zassert_equal(pkt.arm_cmd.hdr.seqNum, 30, "seqNum mismatch");
}

ZTEST(udp_transport_parse, test_parse_sw_safety_cmd) {
  uint8_t buf[UDP_PACKET_SIZE];

  struct SWSafetyCommands cmd = {
      .swCommand = TRAN_EVT_ESTOP,
  };
  size_t pkt_len =
      build_packet(buf, PKT_RX_SW_SAFETY_CMD, &cmd, sizeof(cmd));

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, (ssize_t)pkt_len);

  zassert_equal(pkt.type, PKT_RX_SW_SAFETY_CMD, "wrong packet type");
  zassert_equal(pkt.sw_safety_cmd.swCommand, TRAN_EVT_ESTOP,
                "swCommand mismatch");
}

/* --------------------------------------------------------------------------
 * Edge / error cases
 * -------------------------------------------------------------------------- */

ZTEST(udp_transport_parse, test_parse_too_short_no_header) {
  /* fewer bytes than UDPPacketHeader — must drop */
  uint8_t buf[2] = {0x01, 0x00};

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, sizeof(buf));

  zassert_equal(pkt.type, 0, "should return zeroed pkt when too short");
}

ZTEST(udp_transport_parse, test_parse_too_long) {
  /* buf larger than header + largest possible UDPReceivedPacket — must drop */
  size_t oversized =
      sizeof(struct UDPPacketHeader) + sizeof(struct UDPReceivedPacket) + 1;
  uint8_t buf[512] = {0};

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, (ssize_t)oversized);

  zassert_equal(pkt.type, 0, "should return zeroed pkt when too long");
}

ZTEST(udp_transport_parse, test_parse_payload_too_short_for_type) {
  /* header present but payload is truncated — hits too_short goto */
  uint8_t buf[UDP_PACKET_SIZE] = {0};
  struct UDPPacketHeader hdr = {
      .type = PKT_RX_DRIVE_VEL_CMD, .version = 1, .len = 1};
  memcpy(buf, &hdr, sizeof(hdr));
  /* only 1 byte of payload — far less than sizeof(DriveVelocityCommand) */
  buf[sizeof(hdr)] = 0xFF;

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, sizeof(hdr) + 1);

  zassert_equal(pkt.type, 0, "should return zeroed pkt when payload too short");
}

ZTEST(udp_transport_parse, test_parse_unknown_type) {
  uint8_t buf[UDP_PACKET_SIZE] = {0};
  struct UDPPacketHeader hdr = {.type = 0xFF, .version = 1, .len = 0};
  memcpy(buf, &hdr, sizeof(hdr));

  struct UDPReceivedPacket pkt =
      udp_transport_rx_parse(NULL, buf, sizeof(hdr));

  /* default: falls through and returns pkt with the unknown type set */
  zassert_equal(pkt.type, 0xFF, "should preserve unknown type in returned pkt");
}

ZTEST_SUITE(udp_transport_parse, NULL, NULL, NULL, NULL, NULL);
