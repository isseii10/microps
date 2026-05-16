#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "platform.h"

#include "ip.h"
#include "net.h"
#include "util.h"

#define IP_HDR_FLAG_MF 0x2000 /* more flagments flag */
#define IP_HDR_FLAG_DF 0x4000 /* don't flagment flag */
#define IP_HDR_FLAG_RF 0x8000 /* reserved */

#define IP_HDR_OFFSET_MASK 0x1fff

const ip_addr_t IP_ADDR_ANY = 0x00000000;       /* 0.0.0.0 */
const ip_addr_t IP_ADDR_BROADCAST = 0xffffffff; /* 255.255.255.255 */

/*
 * NOTE: if you want to add/delete the entries after net_run(),
 *       you need to protect these lists with a mutex.
 */
static struct ip_iface *ifaces;

int ip_addr_pton(const char *p, ip_addr_t *n) {
  char *sp, *ep;
  int idx;
  long ret;

  sp = (char *)p;
  for (idx = 0; idx < 4; idx++) {
    ret = strtol(sp, &ep, 10);
    if (ret < 0 || ret > 255) {
      return -1;
    }
    if (ep == sp) {
      return -1;
    }
    if ((idx == 3 && *ep != '\0') || (idx != 3 && *ep != '.')) {
      return -1;
    }
    ((uint8_t *)n)[idx] = ret;
    sp = ep + 1;
  }
  return 0;
}

char *ip_addr_ntop(ip_addr_t n, char *p, size_t size) {
  uint8_t *u8;

  u8 = (uint8_t *)&n;
  snprintf(p, size, "%d.%d.%d.%d", u8[0], u8[1], u8[2], u8[3]);
  return p;
}

struct ip_iface *ip_iface_alloc(const char *unicast, const char *netmask) {
  struct ip_iface *iface;
  iface = memory_alloc(sizeof(*iface));
  if (!iface) {
    errorf("memory_alloc() failure");
    return NULL;
  }
  NET_IFACE(iface)->family = NET_IFACE_FAMILY_IP;
  if (ip_addr_pton(unicast, &iface->unicast) == -1) {
    errorf("ip_addr_pton() failure, addr=%s", unicast);
    memory_free(iface);
    return NULL;
  }
  if (ip_addr_pton(netmask, &iface->netmask) == -1) {
    errorf("ip_addr_pton() failure, addr=%s", netmask);
    memory_free(iface);
    return NULL;
  }
  iface->broadcast = (iface->unicast & iface->netmask) | ~iface->netmask;
  return iface;
}

/*
 * NOTE: must not be call after net_run()
 */
int ip_iface_register(struct net_device *dev, struct ip_iface *iface) {
  char addr1[IP_ADDR_STR_LEN];
  char addr2[IP_ADDR_STR_LEN];
  char addr3[IP_ADDR_STR_LEN];

  infof("dev=%s, %s, %s, %s", dev->name, ip_addr_ntop(iface->unicast, addr1, sizeof(addr1)),
        ip_addr_ntop(iface->netmask, addr2, sizeof(addr2)), ip_addr_ntop(iface->broadcast, addr3, sizeof(addr3)));
  if (net_device_add_iface(dev, NET_IFACE(iface)) == -1) {
    errorf("net_device_add_iface() failure");
    return -1;
  }
  iface->next = ifaces;
  ifaces = iface;
  return 0;
}

struct ip_iface *ip_iface_select(ip_addr_t addr) {
  struct ip_iface *entry;
  for (entry = ifaces; entry; entry = entry->next) {
    if (entry->unicast == addr) {
      break;
    }
  }
  return entry;
}

static void ip_print(const uint8_t *data, size_t len) {
  struct ip_hdr *hdr;
  uint8_t v, hl, hlen;
  uint16_t total, offset;
  char addr[IP_ADDR_STR_LEN];

#ifdef HEXDUMP
  hexdump(stderr, data, len);
#endif
  funlockfile(stderr);
}

static void ip_input(const uint8_t *data, size_t len, struct net_device *dev) {
  struct ip_hdr *hdr;
  uint8_t v;
  uint16_t hlen, total, offset;
  struct ip_iface *iface;
  char addr[IP_ADDR_STR_LEN];

  debugf("dev=%s, len=%zu", dev->name, len);

  if (len < IP_HDR_SIZE_MIN) {
    errorf("too short");
    return;
  }

  hdr = (struct ip_hdr *)data;

  v = hdr->vhl >> 4;
  if (v != IP_VERSION_IPV4) {
    errorf("ip version error: v=%u", v);
    return;
  }

  hlen = (hdr->vhl & 0x0f) << 2;
  if (len < hlen) {
    errorf("header length error: len=%zu < hlen=%u", len, hlen);
    return;
  }

  if (cksum16((uint16_t *)hdr, hlen, 0) != 0) {
    errorf("checksum error");
    return;
  }

  total = ntoh16(hdr->total);
  if (len < total) {
    errorf("total length error: len=%zu < total=%u", len, total);
    return;
  }

  offset = ntoh16(hdr->offset);
  if (offset & IP_HDR_FLAG_MF || offset & IP_HDR_OFFSET_MASK) {
    errorf("flagments does not support");
    return;
  }
  iface = (struct ip_iface *)net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
  if (!iface) {
    /* ignore */
    return;
  }
  if (hdr->dst != iface->unicast) {
    if (hdr->dst != iface->broadcast && hdr->dst != IP_ADDR_BROADCAST) {
      /* ignore: for other host */
      return;
    }
  }
  debugf("permit, dev=%s, iface=%s", dev->name, ip_addr_ntop(iface->unicast, addr, sizeof(addr)));

  ip_print(data, total);
}

int ip_init(void) {
  if (net_protocol_register(NET_PROTOCOL_TYPE_IP, ip_input) == -1) {
    errorf("net_protocol_register() failure");
    return -1;
  }
  return 0;
}
