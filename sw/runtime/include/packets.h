// Copyright 2020 ETH Zurich
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef PACKETS_H
#define PACKETS_H

#include <stdint.h>
#include <string.h>

typedef struct mac_addr {
  uint8_t data[6];
} __attribute__((__packed__)) mac_addr_t;

typedef struct eth_hdr {
  mac_addr_t dest;
  mac_addr_t src;
  uint16_t length;
} __attribute__((__packed__)) eth_hdr_t;

typedef struct ip_hdr {
  // ip-like
  // FIXME: bitfield endianness is purely compiler-dependent
  // we should use bit operations
  uint8_t ihl : 4;
  uint8_t version : 4;
  uint8_t tos;
  uint16_t length;

  uint16_t identification;
  uint16_t offset;

  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;

  uint32_t source_id; // 4
  uint32_t dest_id;   // 4

} __attribute__((__packed__)) ip_hdr_t;

typedef struct udp_hdr {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t length;
  uint16_t checksum;
} __attribute__((__packed__)) udp_hdr_t;

// sPIN Lightweight Message Protocol
typedef struct slmp_hdr {
  uint16_t flags;
  uint32_t msg_id;  // larger than needed, but for alignment purposes (Ethernet header is 6 bytes)
  uint32_t pkt_off; // packet offset in message
} __attribute__((__packed__)) slmp_hdr_t;

/*
typedef struct app_hdr
{ //QUIC-like
    uint64_t            connection_id;
    uint16_t            packet_num;
    uint16_t             frame_type; //frame_type 1: connection closing
} __attribute__((__packed__)) app_hdr_t;
*/
typedef struct pkt_hdr {
  eth_hdr_t eth_hdr;
  ip_hdr_t ip_hdr; // FIXME: assumes ihl=4
  udp_hdr_t udp_hdr;
} __attribute__((__packed__)) pkt_hdr_t;

typedef struct slmp_pkt_hdr {
  eth_hdr_t eth_hdr;
  ip_hdr_t ip_hdr; // FIXME: assumes ihl=4
  udp_hdr_t udp_hdr;
  slmp_hdr_t slmp_hdr;
} __attribute__((__packed__)) slmp_pkt_hdr_t;
#define MKEOM 0x8000
#define MKSYN 0x4000
#define MKACK 0x2000
#define EOM(flags) ((flags) & MKEOM)
#define SYN(flags) ((flags) & MKSYN)
#define ACK(flags) ((flags) & MKACK)

// little endian
static inline uint16_t bswap_16(uint16_t v) {
  return ((v & 0xff) << 8) | (v >> 8);
}
static inline uint32_t bswap_32(uint32_t v) {
  return (bswap_16((uint16_t)v) << 16) | bswap_16(v >> 16);
}
#define htons(x) bswap_16(x)
#define ntohs htons
#define htonl(x) bswap_32(x)
#define ntohl htonl

#define SLMP_PAYLOAD_LEN(hdrs) (ntohs(hdrs->udp_hdr.length) - sizeof(udp_hdr_t) - sizeof(slmp_hdr_t))

// http://www.microhowto.info/howto/calculate_an_internet_protocol_checksum_in_c.html
static inline uint16_t ip_checksum(void *vdata, size_t length) {
  // Cast the data pointer to one that can be indexed.
  char *data = (char *)vdata;

  // Initialise the accumulator.
  uint32_t acc = 0xffff;

  // Handle complete 16-bit blocks.
  for (size_t i = 0; i + 1 < length; i += 2) {
    uint16_t word;
    memcpy(&word, data + i, 2);
    acc += ntohs(word);
    if (acc > 0xffff) {
      acc -= 0xffff;
    }
  }

  // Handle any partial block at the end of the data.
  if (length & 1) {
    uint16_t word = 0;
    memcpy(&word, data + length - 1, 1);
    acc += ntohs(word);
    if (acc > 0xffff) {
      acc -= 0xffff;
    }
  }

  // Return the checksum in network byte order.
  return htons(~acc);
}


#endif /* PACKETS_H */
