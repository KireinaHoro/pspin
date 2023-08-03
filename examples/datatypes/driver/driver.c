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

#include "gdriver.h"

#include "../include/datatypes_host.h"

typedef struct {
  uint8_t *p;
  size_t len;
} packet_descr_t;

packet_descr_t *packets = NULL;
int num_ptrs = 0;
int cur_idx = 0;
int tot_pkts = 0;

// copy
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr,
                    const u_char *packet) {
  if (cur_idx == num_ptrs) {
    // need more pointer space
    num_ptrs *= 2;
    packets = realloc(packets, num_ptrs * sizeof(packet_descr_t));
  }
  packet_descr_t *descr = &packets[cur_idx++];
  uint8_t *pkt_buf = descr->p = malloc(pkthdr->len);
  descr->len = pkthdr->len;
  memcpy(pkt_buf, packet, pkthdr->len);
}

uint32_t fill_packet(uint32_t msg_idx, uint32_t pkt_idx, uint8_t *pkt_buff,
                     uint32_t max_pkt_size, uint32_t *l1_pkt_size) {
  printf("Filling msg_idx=%d pkt_idx=%d cur_idx=%d\n", msg_idx, pkt_idx,
         cur_idx);

  packet_descr_t *pkt = &packets[cur_idx];
  memcpy(pkt_buff, pkt->p, pkt->len);

  ++cur_idx;
  return pkt->len;
}

int main(int argc, char *argv[]) {
  const char *handlers_file = "build/datatypes";
  const char *hh = "datatypes_hh";
  const char *ph = "datatypes_ph";
  const char *th = "datatypes_th";

  int ret = 0;
  int ectx_num;
  gdriver_init(argc, argv, NULL, &ectx_num);

  // initial placeholder of pointers
  packets = calloc(num_ptrs, sizeof(packet_descr_t));

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
  printf("Read %d packets\n", cur_idx);
  tot_pkts = cur_idx;
  cur_idx = 0;

  // load dataloops L2 image
  spin_ec_t *ec = gdriver_get_ectx_mems();
  struct mem_area handler_mem = {
      .addr = ec->handler_mem_addr,
      .size = ec->handler_mem_size,
  };
  void *l2_image;
  size_t l2_image_size;
  uint32_t num_elements, userbuf_size;
  void *ddt_mem_raw =
      prepare_ddt_nicmem(ddt_file, handler_mem, &l2_image, &l2_image_size,
                         &num_elements, &userbuf_size);

  // launch ectx and run
  gdriver_add_ectx(handlers_file, hh, ph, th, fill_packet, l2_image,
                   l2_image_size, NULL, 0);
  gdriver_run();

  free(l2_image);
  free(ddt_mem_raw);

  ret = EXIT_SUCCESS;
fail:
  // we are not freeing packet buffers

  gdriver_fini();

  return ret;
}