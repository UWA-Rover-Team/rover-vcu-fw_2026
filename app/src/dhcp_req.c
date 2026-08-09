#include "dhcp_req.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dhcp_req);

#define DHCP_TIMEOUT_MS   10000
#define STATIC_IP_ADDR    "192.168.8.158"
#define STATIC_IP_NETMASK "255.255.255.0"

static struct net_mgmt_event_callback dhcp_cb;
static atomic_t dhcp_bound = ATOMIC_INIT(0);
static struct k_work_delayable dhcp_timeout_work;
static dhcp_req_ready_cb_t ready_cb;

static void dhcp_fallback_handler(struct k_work *work) {
  if (atomic_get(&dhcp_bound)) {
    return;
  }

  LOG_WRN("DHCP timeout, falling back to static IP: %s", STATIC_IP_ADDR);

  struct net_if *iface = net_if_get_default();
  #if !defined(CONFIG_STATIC_VCU_DEV)
  net_dhcpv4_stop(iface);
  #endif

  struct in_addr addr, netmask;
  net_addr_pton(AF_INET, STATIC_IP_ADDR, &addr);
  net_addr_pton(AF_INET, STATIC_IP_NETMASK, &netmask);

  net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
  net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);

  if (ready_cb) {
    ready_cb(iface);
  }
}

static void dhcp_handler(struct net_mgmt_event_callback *cb,
                         uint64_t mgmt_event, struct net_if *iface) {
  if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND) {
    atomic_set(&dhcp_bound, 1);
    k_work_cancel_delayable(&dhcp_timeout_work);

    char buf[NET_IPV4_ADDR_LEN];
    net_addr_ntop(AF_INET, &iface->config.dhcpv4.requested_ip, buf,
                  sizeof(buf));

    LOG_INF("Received IP Lease: %s", buf);

    if (ready_cb) {
      ready_cb(iface);
    }
  }
}

void dhcp_req_start(dhcp_req_ready_cb_t on_ready) {
  ready_cb = on_ready;
  net_mgmt_init_event_callback(&dhcp_cb, dhcp_handler,
                               NET_EVENT_IPV4_DHCP_BOUND);
  net_mgmt_add_event_callback(&dhcp_cb);
  #if defined(CONFIG_STATIC_VCU_DEV)
  LOG_INF("Static IP configuration enabled, skipping DHCP");
  k_work_init_delayable(&dhcp_timeout_work, dhcp_fallback_handler);
  k_work_schedule(&dhcp_timeout_work, K_MSEC(0));
  #else

  struct net_if *iface = net_if_get_default();
  net_dhcpv4_start(iface);

  k_work_init_delayable(&dhcp_timeout_work, dhcp_fallback_handler);
  k_work_schedule(&dhcp_timeout_work, K_MSEC(DHCP_TIMEOUT_MS));
  #endif
}
