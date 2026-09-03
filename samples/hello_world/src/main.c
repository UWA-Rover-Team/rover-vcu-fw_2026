#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(hello_world);

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};

static void hello_timer_handler(struct k_timer *timer)
{
	LOG_INF("Hello, World!");
}

static void led_timer_handler(struct k_timer *timer)
{
	static bool state;

	state = !state;
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		gpio_pin_set_dt(&leds[i], state);
	}
}

K_TIMER_DEFINE(hello_timer, hello_timer_handler, NULL);
K_TIMER_DEFINE(led_timer, led_timer_handler, NULL);

int main(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
	}

	k_timer_start(&hello_timer, K_SECONDS(1), K_SECONDS(1));
	k_timer_start(&led_timer, K_MSEC(333), K_MSEC(333));
	return 0;
}
