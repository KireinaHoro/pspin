#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <pcap.h>
#include <pcap/pcap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gdriver.h"
#include "pspinsim.h"

#include "../include/datatypes_host.h"

#define LOG(msg, ...) printf("[datatypes driver] " msg, __VA_ARGS__)

#define NUM_MSGS 16 // maximum

typedef struct {
  uint8_t *p;
  size_t len;
  bool is_eom;
} packet_descr_t;

typedef struct {
  packet_descr_t *packets;
  int num_ptrs;
  int cur_idx;
  bool present;
} msg_descr_t;
msg_descr_t msgs[NUM_MSGS];

// copy
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr,
                    const u_char *packet) {
  // only send outgoing slmp packets
  const slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)packet;
  if (ntohs(hdrs->eth_hdr.length) != ETHERTYPE_IP)
    return;
  if (hdrs->ip_hdr.protocol != IPPROTO_UDP)
    return;
  if (ntohs(hdrs->udp_hdr.dst_port) != SLMP_PORT)
    return;

  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  int msgid = hdrs->slmp_hdr.msg_id & 0xff;
  msgs[msgid].present = true;

  if (msgs[msgid].cur_idx == msgs[msgid].num_ptrs) {
    // need more pointer space
    msgs[msgid].num_ptrs *= 2;
    msgs[msgid].packets = realloc(
        msgs[msgid].packets, msgs[msgid].num_ptrs * sizeof(packet_descr_t));
  }
  packet_descr_t *descr = &msgs[msgid].packets[msgs[msgid].cur_idx++];
  uint8_t *pkt_buf = descr->p = malloc(pkthdr->len);
  descr->len = pkthdr->len;
  descr->is_eom = flags & 0x8000; // EOM bit in SLMP
  memcpy(pkt_buf, packet, pkthdr->len);
}

typedef struct {
  spin_ec_t *ec;
  int msgid;
  int pktidx;
} feedback_args_t;

void interactive_cb(uint64_t user_ptr, uint64_t nic_arrival_time,
                    uint64_t pspin_arrival_time, uint64_t feedback_time) {
  feedback_args_t *fb_args = (feedback_args_t *)user_ptr;
  msg_descr_t *msg = &msgs[fb_args->msgid];

  LOG("Finished packet #%d of message #%d\n", fb_args->pktidx, fb_args->msgid);

  int next_idx = ++fb_args->pktidx;
  if (next_idx == msg->cur_idx) {
    LOG("Finished message #%d\n", fb_args->msgid);
    msg->present = false;

    bool remaining = false;
    for (int i = 0; i < NUM_MSGS; ++i) {
      if (msgs[i].present) {
        remaining = true;
        break;
      }
    }
    if (!remaining) {
      pspinsim_packet_eos();
    }

    free(fb_args);
    return;
  }

  // send rest of message
  packet_descr_t *pkt = &msg->packets[next_idx];

  // wait random number of cycles to simulate real world situation
  int wait = 0; //rand() % 200;
  LOG("Adding next packet with %d cycles delay\n", wait);

  pspinsim_packet_add(fb_args->ec, fb_args->msgid, pkt->p, pkt->len, pkt->len,
                      pkt->is_eom, wait, user_ptr);
}

int main(int argc, char *argv[]) {
  const char *handlers_file = "build/datatypes";
  const char *hh = "datatypes_hh";
  const char *ph = "datatypes_ph";
  const char *th = "datatypes_th";

  int ret = 0;
  int ectx_num;
  gdriver_init(argc, argv, NULL, &ectx_num);

  int seed = time(NULL);
  LOG("random seed: %d\n", seed);
  srand(seed);

  if (!gdriver_is_interactive()) {
    fprintf(stderr, "datatypes host only supports interactive mode\n");
    return EXIT_FAILURE;
  }

  // initial placeholder of pointers
  for (int i = 0; i < NUM_MSGS; ++i) {
    msgs[i].num_ptrs = 1;
    msgs[i].packets = calloc(msgs[i].num_ptrs, sizeof(packet_descr_t));
  }

  // get packet pcap
  const char *pcap_file = getenv("DDT_PCAP");
  if (!pcap_file) {
    fprintf(stderr, "DDT_PCAP not set\n");
    ret = EXIT_FAILURE;
    goto fail;
  }

  // get ddt description
  const char *ddt_file = getenv("DDT_BIN");
  if (!ddt_file) {
    fprintf(stderr, "DDT_BIN not set\n");
    ret = EXIT_FAILURE;
    goto fail;
  }

  // read packet trace
  pcap_t *fp;
  char errbuf[PCAP_ERRBUF_SIZE];
  fp = pcap_open_offline(pcap_file, errbuf);
  if (!fp) {
    fprintf(stderr, "pcap_open_offline() failed: %s\n", errbuf);
    ret = EXIT_FAILURE;
    goto fail;
  }
  if (pcap_loop(fp, 0, packet_handler, NULL) < 0) {
    fprintf(stderr, "pcap_loop() failed: %s\n", pcap_geterr(fp));
    ret = EXIT_FAILURE;
    goto fail;
  }
  for (int i = 0; i < NUM_MSGS; ++i) {
    if (msgs[i].cur_idx) {
      LOG("SLMP Message #%d: %d packets\n", i, msgs[i].cur_idx);
    }
  }

  // load dataloops L2 image
  spin_ec_t *ec = gdriver_get_ectx_mems();

  struct mem_area handler_mem = {
      .addr = ec->handler_mem_addr,
      .size = ec->handler_mem_size,
  };
  void *l2_image;
  size_t l2_image_size;
  uint32_t num_elements, userbuf_size;
  uint32_t streambuf_size;
  void *ddt_mem_raw =
      prepare_ddt_nicmem(ddt_file, handler_mem, &l2_image, &l2_image_size,
                         &num_elements, &userbuf_size, &streambuf_size);

  // install ectx
  gdriver_add_ectx(handlers_file, hh, ph, th, NULL, l2_image, l2_image_size,
                   NULL, 0);

  // send head of messages
  for (int i = 0; i < NUM_MSGS; ++i) {
    if (!msgs[i].present)
      continue;
    packet_descr_t *pkt = &msgs[i].packets[0];

    // msgid as user_ptr
    feedback_args_t *arg = malloc(sizeof(feedback_args_t));
    arg->msgid = i;
    arg->pktidx = 0;
    arg->ec = ec;

    pspinsim_packet_add(ec, i, pkt->p, pkt->len, pkt->len, pkt->is_eom,
                        0/*rand() % 100*/, (uint64_t)arg);
  }

  // set interactive callback
  pspinsim_cb_set_pkt_feedback(interactive_cb);

  // start simulation
  gdriver_run();

  free(l2_image);
  free(ddt_mem_raw);

  ret = EXIT_SUCCESS;
fail:
  // we are not freeing packet buffers

  gdriver_fini();

  return ret;
}