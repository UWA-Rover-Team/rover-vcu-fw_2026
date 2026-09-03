#ifndef CANUDP_PROTO_H
#define CANUDP_PROTO_H

#include <stdint.h>
#include <string.h>

/*
 * Fixed 25-byte frame, one per UDP datagram. Matches the board's layout:
 *
 *   BYTE 0:     Protocol version
 *   BYTE 1:     Flags (EFF/RTR/ERR - see CANUDP_FLAG_*)
 *   BYTE 2-7:   Reserved (future timestamp)
 *   BYTE 8-12:  CAN ID, big-endian. Byte 8 is currently unused/reserved
 *               (29-bit extended ID only needs 4 bytes); free for later use
 *               (bus index, priority, etc).
 *   BYTE 13:    Data length, 0-8
 *   BYTE 14:    FD flags: 0 = classic CAN frame, non-zero = CAN FD
 *               (see CANUDP_FDFLAG_*)
 *   BYTE 15-16: Reserved (future timestamp)
 *   BYTE 17-24: Data, 8 bytes. NOTE: this is a cut-down FD profile -
 *               only the first 8 data bytes are carried even for FD
 *               frames (no support yet for the 12/16/20/24/32/48/64
 *               byte FD payload lengths).
 *
 * There's no magic number in this version - validity is checked via
 * (a) exact datagram length and (b) version byte. If you want stronger
 * protection against garbage/misrouted packets, one of the reserved
 * bytes could carry a magic value later.
 *
 * NOTE: this file must stay byte-for-byte identical between the board
 * firmware and canudp_daemon on the host - keep them in sync (e.g. a
 * shared submodule) rather than hand-copying edits to one side.
 */

#define CANUDP_VERSION 1

/* BYTE 1 - top-level frame flags */
#define CANUDP_FLAG_EFF 0x01u /* extended (29-bit) ID */
#define CANUDP_FLAG_RTR 0x02u /* remote transmission request */
#define CANUDP_FLAG_ERR 0x04u /* error frame (reserved, not handled yet) */

/* BYTE 14 - CAN FD flags. Zero means "classic CAN frame". */
#define CANUDP_FDFLAG_FDF 0x01u /* this is an FD frame, not classic       */
#define CANUDP_FDFLAG_BRS 0x02u /* bit rate switch                        */
#define CANUDP_FDFLAG_ESI 0x04u /* error state indicator                  */

#define CANUDP_ID_BYTES 5
#define CANUDP_DATA_LEN 8

struct __attribute__((packed)) canudp_frame {
  uint8_t version;                 /* offset 0      */
  uint8_t flags;                   /* offset 1      */
  uint8_t reserved0[6];            /* offset 2-7    */
  uint8_t can_id[CANUDP_ID_BYTES]; /* offset 8-12   */
  uint8_t len;                     /* offset 13     */
  uint8_t fd_flags;                /* offset 14     */
  uint8_t reserved1[2];            /* offset 15-16  */
  uint8_t data[CANUDP_DATA_LEN];   /* offset 17-24  */
};

#define CANUDP_FRAME_SIZE (sizeof(struct canudp_frame)) /* 25 bytes */

/* Pack/unpack the 5-byte big-endian CAN ID field. Byte 0 of the field
 * (overall offset 8) is left as reserved/zero for now. */
static inline void canudp_pack_id(uint8_t out[CANUDP_ID_BYTES], uint32_t id) {
  out[0] = 0;
  out[1] = (uint8_t)(id >> 24);
  out[2] = (uint8_t)(id >> 16);
  out[3] = (uint8_t)(id >> 8);
  out[4] = (uint8_t)(id);
}

static inline uint32_t canudp_unpack_id(const uint8_t in[CANUDP_ID_BYTES]) {
  return ((uint32_t)in[1] << 24) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 8) | (uint32_t)in[4];
}

#endif /* CANUDP_PROTO_H */