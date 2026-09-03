#ifndef DHCP_REQ_H_
#define DHCP_REQ_H_

#include <zephyr/net/net_if.h>

typedef void (*dhcp_req_ready_cb_t)(struct net_if *iface);

void dhcp_req_start(dhcp_req_ready_cb_t on_ready);

#endif /* DHCP_REQ_H_ */
