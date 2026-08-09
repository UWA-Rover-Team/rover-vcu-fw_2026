#include "dhcp_req.h"
#include "udp_transport.h"
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

static void log_timer_handler(struct k_timer *timer) {
  LOG_DBG("Hello, World from Zephyr!");
}

K_TIMER_DEFINE(log_timer, log_timer_handler, NULL);

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static void led_timer_handler(struct k_timer *timer) {
  gpio_pin_toggle_dt(&led0);
  gpio_pin_toggle_dt(&led1);
  gpio_pin_toggle_dt(&led2);
}

K_TIMER_DEFINE(led_timer, led_timer_handler, NULL);

struct UDPTransport udp_transport;

static void on_network_ready(struct net_if *iface) {
  udp_transport_init(&udp_transport);
}

int main(void) {

  LOG_INF("Starting UDP to CAN Bus Gateway");

  gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
  k_timer_start(&led_timer, K_NO_WAIT, K_MSEC(200));

  dhcp_req_start(on_network_ready);

  k_timer_start(&log_timer, K_MSEC(1000), K_MSEC(1000));
  return 0;
}
