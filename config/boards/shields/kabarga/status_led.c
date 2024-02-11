#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zmk/ble.h>
#include <zmk/usb.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/battery.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <math.h>
#include <stdlib.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/hid_indicators.h>

#include <zephyr/drivers/led_strip.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/workqueue.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_LED_NODE DT_NODELABEL(status_led)
#define BAT_LED_NODE_1 DT_NODELABEL(bat_led_1)
#define BAT_LED_NODE_2 DT_NODELABEL(bat_led_2)
#define BAT_LED_NODE_3 DT_NODELABEL(bat_led_3)

#define LED_BLINK_PROFILE 180
#define LED_BLINK_CONN 140
#define LED_BATTERY_BLINK 200
#define LED_BATTERY_SHOW 1400
#define LED_BATTERY_SLEEP_SHOW 1000
#define LED_STATUS_ON 1
#define LED_STATUS_OFF 0

// #define real_charging_animation
#define disable_led_sleep_pc
#define show_bat_status_all_time
#define show_led_idle

int level_one = 0;

struct led
{
    const struct device *gpio_dev;
    unsigned int gpio_pin;
    unsigned int gpio_flags;
};

struct led status_led = {
    .gpio_dev = DEVICE_DT_GET(DT_GPIO_CTLR(STATUS_LED_NODE, gpios)),
    .gpio_pin = DT_GPIO_PIN(STATUS_LED_NODE, gpios),
    .gpio_flags = GPIO_OUTPUT | DT_GPIO_FLAGS(STATUS_LED_NODE, gpios),
};

enum
{
    BAT_1,
    BAT_2,
    BAT_3
};

struct led battery_leds[] = {
    [BAT_1] = {
        .gpio_dev = DEVICE_DT_GET(DT_GPIO_CTLR(BAT_LED_NODE_1, gpios)),
        .gpio_pin = DT_GPIO_PIN(BAT_LED_NODE_1, gpios),
        .gpio_flags = GPIO_OUTPUT | DT_GPIO_FLAGS(BAT_LED_NODE_1, gpios),
    },
    [BAT_2] = {
        .gpio_dev = DEVICE_DT_GET(DT_GPIO_CTLR(BAT_LED_NODE_2, gpios)),
        .gpio_pin = DT_GPIO_PIN(BAT_LED_NODE_2, gpios),
        .gpio_flags = GPIO_OUTPUT | DT_GPIO_FLAGS(BAT_LED_NODE_2, gpios),
    },
    [BAT_3] = {
        .gpio_dev = DEVICE_DT_GET(DT_GPIO_CTLR(BAT_LED_NODE_3, gpios)),
        .gpio_pin = DT_GPIO_PIN(BAT_LED_NODE_3, gpios),
        .gpio_flags = GPIO_OUTPUT | DT_GPIO_FLAGS(BAT_LED_NODE_3, gpios),
    },
};

static inline void ledON(const struct led *led) { gpio_pin_set(led->gpio_dev, led->gpio_pin, LED_STATUS_ON); }
static inline void ledOFF(const struct led *led) { gpio_pin_set(led->gpio_dev, led->gpio_pin, LED_STATUS_OFF); }

static void led_all_OFF()
{
    ledOFF(&status_led);
    for (int i = 0; i < (sizeof(battery_leds) / sizeof(struct led)); i++)
    {
        ledOFF(&battery_leds[i]);
    }
};

void led_configure(const struct led *led)
{
    int ret = gpio_pin_configure(led->gpio_dev, led->gpio_pin, led->gpio_flags);
    if (ret != 0)
    {
        printk("Error %d: failed to configure pin %d\n", ret, led->gpio_pin);
        return;
    }

    ledOFF(led);
}

void blink(const struct led *led, uint32_t sleep_ms, const int count)
{
    for (int i = 0; i < count; i++)
    {
        ledON(led);
        k_msleep(sleep_ms);
        ledOFF(led);
        k_msleep(sleep_ms);
    }
}

void blink_3_led(uint32_t sleep_ms, const int count)
{
    for (int i = 0; i < count; i++)
    {
        ledON(&battery_leds[0]);
        ledON(&battery_leds[1]);
        ledON(&battery_leds[2]);
        k_msleep(sleep_ms);
        ledOFF(&battery_leds[0]);
        ledOFF(&battery_leds[1]);
        ledOFF(&battery_leds[2]);
        k_msleep(sleep_ms);
    }
}

void blink_once(const struct led *led, uint32_t sleep_ms)
{
    ledON(led);
    k_msleep(sleep_ms);
    ledOFF(led);
}

// Running charging animation
struct k_timer bat_timer;
int led_i = 0;
void led_bat_animation()
{

#ifdef show_bat_status_all_time
    enum zmk_usb_conn_state usb_status_con = zmk_usb_get_conn_state();
    if (usb_status_con == ZMK_USB_CONN_NONE)
    {
        led_all_OFF();
        return;
    }
#endif

#ifdef disable_led_sleep_pc
    enum usb_dc_status_code usb_status_suspend_ = zmk_usb_get_status();
    if (usb_status_suspend_ == USB_DC_SUSPEND)
    {
        led_all_OFF();
        return;
    }
#endif

#ifdef real_charging_animation
    uint8_t level = zmk_battery_state_of_charge();
    LOG_WRN("Battery %d", level_one);

    if (led_i == 0)
    {
        led_all_OFF();
        led_i = 1;
    }
    else
    {
        if (level <= 20)
        {
            blink(&battery_leds[0], LED_BATTERY_BLINK, 5);
        }
        else
        {
            ledON(&battery_leds[0]);
            if (level > 40)
            {
                ledON(&battery_leds[1]);
            }
            if (level > 80)
            {
                ledON(&battery_leds[2]);
            }
        }
        led_i = 0;
    }
#else
    switch (led_i)
    {
    case 1:
        ledON(&battery_leds[0]);
        led_i++;
        break;
    case 2:
        ledON(&battery_leds[1]);
        led_i++;
        break;
    case 3:
        ledON(&battery_leds[2]);
        led_i = 0;
        break;
    case 0:
        led_all_OFF();
        led_i++;
        break;
    }
#endif
    k_timer_start(&bat_timer, K_SECONDS(LED_BATTERY_SLEEP_SHOW / 1000), K_NO_WAIT);
}

void led_bat_handler(struct k_work *work)
{
    enum zmk_activity_state state = zmk_activity_get_state();
    if (state == ZMK_ACTIVITY_ACTIVE)
    {
        return;
    }

    led_bat_animation();
}
K_WORK_DEFINE(led_bat_worker, led_bat_handler);

void led_bat_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&led_bat_worker);
}
K_TIMER_DEFINE(bat_timer, led_bat_timer_handler, NULL);

// Checking the connection status
struct k_timer led_timer;
bool led_conn_check_working = false;

void check_ble_connection()
{
    if (zmk_ble_active_profile_is_connected())
    {
        led_conn_check_working = false;
    }
    else
    {
        enum usb_dc_status_code usb_status = zmk_usb_get_status();
        if (usb_status == USB_DC_CONNECTED)
        {
            return;
        }

        blink_once(&status_led, LED_BLINK_CONN);
        led_conn_check_working = true;
        // Restart timer for next status check
        k_timer_start(&led_timer, K_SECONDS(4), K_NO_WAIT);
    }
}

void led_check_connection_handler(struct k_work *work)
{
    enum zmk_activity_state state = zmk_activity_get_state();
    if (state != ZMK_ACTIVITY_ACTIVE)
    {
        return;
    }
    check_ble_connection();
}

K_WORK_DEFINE(led_check_conn, led_check_connection_handler);

void led_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&led_check_conn);
}
K_TIMER_DEFINE(led_timer, led_timer_handler, NULL);

void my_work_handler(struct k_work *work);
K_WORK_DEFINE(my_work, my_work_handler);
void my_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&my_work);
}
K_TIMER_DEFINE(my_timer, my_timer_handler, NULL);
void my_work_handler(struct k_work *work)
{
    uint8_t level = zmk_battery_state_of_charge();

    if (level != 0)
    {
        k_timer_stop(&my_timer);
        if (level <= 15)
        {
            blink(&battery_leds[0], LED_BATTERY_BLINK, 5);
        }
        else if (level > 15)
        {
            ledON(&battery_leds[0]);
        }
        else if (level > 30)
        {
            ledON(&battery_leds[0]);
            blink(&battery_leds[1], LED_BATTERY_BLINK, 5);
        }
        else if (level > 50)
        {
            ledON(&battery_leds[0]);
            ledON(&battery_leds[1]);
        }
        else if (level > 70)
        {
            ledON(&battery_leds[0]);
            ledON(&battery_leds[1]);
            blink(&battery_leds[2], LED_BATTERY_BLINK, 5);
        }
        else if (level == 100)
        {
            blink_3_led(LED_BATTERY_BLINK, 5)
        }
        k_msleep(LED_BATTERY_SHOW);
        led_all_OFF();
    }
    else
    {
        // NOTE(sqd): Basically timer will go on and on until we get level different that zero.
    }
}

static int led_init(const struct device *dev)
{
    led_configure(&status_led);

    for (int i = 0; i < (sizeof(battery_leds) / sizeof(struct led)); i++)
    {
        led_configure(&battery_leds[i]);
    }
    k_timer_start(&my_timer, K_NO_WAIT, K_SECONDS(1));
    check_ble_connection();
    return 0;
}

SYS_INIT(led_init, APPLICATION, 32);

// Show leds on profile changing
int led_profile_listener(const zmk_event_t *eh)
{
    const struct zmk_ble_active_profile_changed *profile_ev = NULL;
    if ((profile_ev = as_zmk_ble_active_profile_changed(eh)) == NULL)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }
    // For profiles 1-3 blink appropriate leds.
    if (profile_ev->index <= 2)
    {
        for (int i = 0; i <= profile_ev->index; i++)
        {
            ledON(&battery_leds[i]);
        }
        k_msleep(LED_BLINK_PROFILE);
        led_all_OFF();
    }

    if (!led_conn_check_working)
    {
        check_ble_connection();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(led_profile_status, led_profile_listener)
ZMK_SUBSCRIPTION(led_profile_status, zmk_ble_active_profile_changed);

// Restore activity after return to active state
int led_state_listener(const zmk_event_t *eh)
{
    enum zmk_activity_state state = zmk_activity_get_state();

    if (state == ZMK_ACTIVITY_ACTIVE && !led_conn_check_working)
    {
        check_ble_connection();
    }
    // CONFIG_ZMK_IDLE_TIMEOUT Default 30sec
#ifdef show_led_idle
    if (state != ZMK_ACTIVITY_ACTIVE)
    {
        led_bat_animation();
    }
    else
    {
        led_all_OFF();
    }
#else
    led_bat_animation();
#endif
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(led_activity_state, led_state_listener)
ZMK_SUBSCRIPTION(led_activity_state, zmk_activity_state_changed);

// https://github.com/zmkfirmware/zmk/blob/5826b80374625d448cfbfc739dde4fda1e6f2681/app/src/usb.c#L33

// https://github.com/zmkfirmware/zmk/blob/b8846cf6355c5d7ae52a191988054b532a264f0c/app/dts/behaviors/reset.dtsi#L12
// https://github.com/zmkfirmware/zmk/blob/b8846cf6355c5d7ae52a191988054b532a264f0c/app/src/behaviors/behavior_reset.c

// ToDO: add KeyCode for display battery
// O OFF
// B BLINK
// X ON
// -------------------
// OBBO BOOTLOADER
// -------------------
// Bluetooth
// XOOO BT1
// OXOO BT2
// OOXO BT3
// OOOX Connection lost
// -------------------
// CHARGING
// -------------------
// VAR1 animation, without bat status
// -------------------
// XOOO
// XXOO
// XXXO
// OOOO
// -------------------
// VAR2
// -------------------
// BBBO 100%
// XXBO >70%
// XXOO >50%
// XBOO >15%
// BOOO <15%
// -------------------
// VAR2
// -------------------
// BBBO 100%
// XXXO >80%
// XXBO >50%
// XBOO >15%
// XOOO >15%
// BOOO <15%