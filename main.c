#ifndef LINUX
#define LINUX
#endif

#ifndef __USE_MISC
#define __USE_MISC
#endif

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define FRAME_SIZE 64
#define PAYLOAD_OFFSET 14
#define FINGERPRINT_SIZE 32

#define ETHER_TYPE_CUSTOM ETH_P_LOOPBACK

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

typedef struct {
  int idx;
  char name[IFNAMSIZ + 1];
  unsigned char mac[6];
} iface_t;

typedef struct {
  char iface[IFNAMSIZ + 1];
  unsigned char src_mac[6];
  unsigned char fingerprint[FINGERPRINT_SIZE + 1];
  int timeout_sec;
} thread_args_t;

pthread_t threads[64];
thread_args_t thread_args[64];
iface_t interfaces[64];
int iface_count = 0;

void gen_fingerprint(unsigned char *fingerprint, size_t size) {
  if (size == 0)
    return;
  for (size_t i = 0; i < size - 1; i++) {
    fingerprint[i] = rand() % 256;
  }
  fingerprint[size - 1] = '\0';
}

int get_mac(const char *iface, unsigned char *mac) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }

  struct ifreq ifr;
  strncpy(ifr.ifr_name, iface, IFNAMSIZ);
  if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
    perror("ioctl");
    close(fd);
    return -1;
  }

  memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
  close(fd);
  return 0;
}

int send_frame(int sock, const char *iface, unsigned char *src_mac,
               unsigned char *dst_mac, unsigned char *fingerprint) {
  struct sockaddr_ll device = {0};
  device.sll_ifindex = if_nametoindex(iface);
  if (device.sll_ifindex == 0) {
    perror("if_nametoindex");
    return -1;
  }

  device.sll_family = AF_PACKET;
  device.sll_protocol = htons(ETHER_TYPE_CUSTOM);
  memcpy(device.sll_addr, src_mac, 6);
  device.sll_halen = ETH_ALEN;

  unsigned char frame[FRAME_SIZE] = {0};
  struct ether_header *frame_hdr = (struct ether_header *)frame;

  memcpy(frame_hdr->ether_dhost, dst_mac, 6); // Destination MAC
  memcpy(frame_hdr->ether_shost, src_mac, 6); // Source MAC
  frame_hdr->ether_type = htons(ETHER_TYPE_CUSTOM);
  memcpy(frame + PAYLOAD_OFFSET, fingerprint, FINGERPRINT_SIZE); // Payload

  if (sendto(sock, frame, FRAME_SIZE, 0, (struct sockaddr *)&device,
             sizeof(device)) < 0) {
    perror("sendto");
    return -1;
  }

  printf("iface: %s: test frame sent with fingerprint: ", iface);
  for (int i = 0; i < FINGERPRINT_SIZE; i++) {
    printf("%02x", fingerprint[i]);
  }
  printf("\n");
  return 0;
}

int recv_frame(int sock, unsigned char *src_mac, unsigned char *fingerprint,
               int timeout_sec) {
  unsigned char buffer[FRAME_SIZE];
  struct sockaddr_ll device = {0};
  socklen_t len = sizeof(device);

  fd_set read_fds;
  struct timeval timeout;

  FD_ZERO(&read_fds);
  FD_SET(sock, &read_fds);
  timeout.tv_sec = timeout_sec;
  timeout.tv_usec = 0;

  int result = select(sock + 1, &read_fds, NULL, NULL, &timeout);
  if (result <= 0) {
    if (result == 0)
      printf("Timeout reached. No loop detected.\n");
    else
      perror("select");
    return 0;
  }

  ssize_t num_bytes =
      recvfrom(sock, buffer, FRAME_SIZE, 0, (struct sockaddr *)&device, &len);
  if (num_bytes < 0) {
    perror("recvfrom");
    return -1;
  }

  if (memcmp(buffer + 6, src_mac, 6) == 0 &&
      ntohs(*(unsigned short *)(buffer + 12)) == ETHER_TYPE_CUSTOM &&
      memcmp(buffer + PAYLOAD_OFFSET, fingerprint, FINGERPRINT_SIZE) == 0) {
    char iface[IFNAMSIZ + 1] = {0};
    if (if_indextoname(device.sll_ifindex, iface) == NULL) {
      perror("if_indextoname");
      return -1;
    }

    printf("iface: %s: LOOP DETECTED!!! fingerprint matched.\n", iface);
    return 1;
  }
  return 0;
}

void *thread_func(void *arg) {
  thread_args_t *args = (thread_args_t *)arg;

  int sock = socket(AF_PACKET, SOCK_RAW, htons(ETHER_TYPE_CUSTOM));
  if (sock < 0) {
    perror("socket");
    pthread_exit(NULL);
  }

  send_frame(sock, args->iface, args->src_mac,
             (unsigned char[]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
             args->fingerprint);

  int ret =
      recv_frame(sock, args->src_mac, args->fingerprint, args->timeout_sec);

  close(sock);
  pthread_exit((void *)(intptr_t)ret);
}

int get_ifaces(iface_t *interfaces, int *count) {
  int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sock < 0) {
    perror("socket");
    return -1;
  }

  struct sockaddr_nl addr = {0};
  addr.nl_family = AF_NETLINK;
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(sock);
    return -1;
  }

  struct {
    struct nlmsghdr nlh;
    struct rtgenmsg rtgen;
  } request = {0};
  request.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
  request.nlh.nlmsg_type = RTM_GETLINK;
  request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  request.rtgen.rtgen_family = AF_PACKET;

  if (send(sock, &request, request.nlh.nlmsg_len, 0) < 0) {
    perror("send");
    close(sock);
    return -1;
  }

  char buffer[8192];
  struct iovec iov = {buffer, sizeof(buffer)};
  struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};

  *count = 0;
  while (1) {
    ssize_t len = recvmsg(sock, &msg, 0);
    if (len <= 0)
      break;

    struct nlmsghdr *nh = (struct nlmsghdr *)buffer;
    for (; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
      if (nh->nlmsg_type == NLMSG_DONE) {
        close(sock);
        return 0;
      }
      if (nh->nlmsg_type == NLMSG_ERROR) {
        fprintf(stderr, "Netlink error\n");
        close(sock);
        return -1;
      }

      struct ifinfomsg *ifi = NLMSG_DATA(nh);
      struct rtattr *attr = IFLA_RTA(ifi);
      int attr_len = nh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi));

      iface_t iface_data = {0};
      iface_data.idx = ifi->ifi_index;

      for (; RTA_OK(attr, attr_len); attr = RTA_NEXT(attr, attr_len)) {
        if (attr->rta_type == IFLA_IFNAME) {
          strncpy(iface_data.name, (char *)RTA_DATA(attr), IFNAMSIZ);
          // printf("iface: %s\n", iface_data.name);

        } else if (attr->rta_type == IFLA_ADDRESS) {
          memcpy(iface_data.mac, RTA_DATA(attr), 6);
        }
      }

      if (strlen(iface_data.name) == 0 || strcmp(iface_data.name, "lo") == 0)
        continue;

      memcpy(&interfaces[(*count)++], &iface_data, sizeof(iface_data));
    }
  }

  close(sock);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s <interface|any> [timeout_seconds]\n", argv[0]);
    return EXIT_FAILURE;
  }

  int timeout_sec = (argc == 3) ? atoi(argv[2]) : 10;
  if (timeout_sec <= 0) {
    fprintf(stderr, "Invalid timeout value.\n");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "any") == 0) {
    if (get_ifaces(interfaces, &iface_count) != 0) {
      fprintf(stderr, "Failed to get interfaces.\n");
      return EXIT_FAILURE;
    }
    printf("found ifaces: %d\n", iface_count);
  } else {
    iface_count = 1;
    strncpy(interfaces[0].name, argv[1], IFNAMSIZ);
    if (get_mac(argv[1], interfaces[0].mac) != 0) {
      return EXIT_FAILURE;
    }
    interfaces[0].idx = if_nametoindex(argv[1]);
    if (interfaces[0].idx == 0) {
      perror("if_nametoindex");
      return EXIT_FAILURE;
    }
  }

  for (int i = 0; i < iface_count; i++) {
    unsigned char fingerprint[FINGERPRINT_SIZE + 1];
    gen_fingerprint(fingerprint, FINGERPRINT_SIZE);

    thread_args[i].timeout_sec = timeout_sec;
    strncpy(thread_args[i].iface, interfaces[i].name, IFNAMSIZ);
    memcpy(thread_args[i].src_mac, interfaces[i].mac, 6);
    memcpy(thread_args[i].fingerprint, fingerprint, FINGERPRINT_SIZE);

    if (pthread_create(&threads[i], NULL, thread_func, &thread_args[i]) != 0) {
      perror("pthread_create");
    }
  }

  int ret = 0;
  for (int i = 0; i < iface_count; i++) {
    void *retval;
    if (pthread_join(threads[i], &retval) == 0) {
      if (retval != NULL && (intptr_t)retval != EXIT_SUCCESS) {
        ret++; // if loop detected
      }
    } else {
      perror("pthread_join");
    }
  }

  return ret;
}
