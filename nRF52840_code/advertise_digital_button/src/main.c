#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>

/* GPIO from devicetree node label sw1 (see overlay) */
static const struct gpio_dt_spec btn =
    GPIO_DT_SPEC_GET(DT_NODELABEL(sw1), gpios);

static struct gpio_callback btn_cb_data;
static uint8_t gpio_state;
static bool notify_enabled;

/* 128-bit UUIDs (example values) */
static struct bt_uuid_128 gpio_svc_uuid =
    BT_UUID_INIT_128(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0xf0);

static struct bt_uuid_128 gpio_char_uuid =
    BT_UUID_INIT_128(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
                     0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0xf1);

/* Read callback: returns current button state (0 = pressed, 1 = released) */
static ssize_t read_gpio(struct bt_conn *conn,
                         const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset)
{
    int val = gpio_pin_get_dt(&btn);
    if (val < 0) {
        val = 0;
    }
    gpio_state = (uint8_t)val;

    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             &gpio_state, sizeof(gpio_state));
}

/* CCC (Client Characteristic Configuration) changed: notifications on/off */
static void gpio_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* GATT service definition */
BT_GATT_SERVICE_DEFINE(gpio_svc,
    BT_GATT_PRIMARY_SERVICE(&gpio_svc_uuid),
    BT_GATT_CHARACTERISTIC(&gpio_char_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_gpio, NULL, &gpio_state),
    BT_GATT_CCC(gpio_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* GPIO interrupt callback: send notification when SW1 changes */
static void button_pressed(const struct device *dev,
                           struct gpio_callback *cb,
                           uint32_t pins)
{
    int val = gpio_pin_get_dt(&btn);
    if (val < 0) {
        return;
    }

    gpio_state = (uint8_t)val;

    if (notify_enabled) {
        /* Attribute index 1 contains our characteristic value */
        bt_gatt_notify(NULL, &gpio_svc.attrs[1],
                       &gpio_state, sizeof(gpio_state));
    }
}

/* Advertising payload */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* You can add service UUID here if you want it in the advert */
};

void main(void)
{
    int err;

    printk("Starting nRF52840 GPIO + BLE example\n");

    /* --- GPIO init --- */
    if (!gpio_is_ready_dt(&btn)) {
        printk("Button device not ready\n");
        return;
    }

    err = gpio_pin_configure_dt(&btn, GPIO_INPUT);
    if (err) {
        printk("Failed to config button: %d\n", err);
        return;
    }

    err = gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_BOTH);
    if (err) {
        printk("Failed to config button interrupt: %d\n", err);
        return;
    }

    gpio_init_callback(&btn_cb_data, button_pressed, BIT(btn.pin));
    gpio_add_callback(btn.port, &btn_cb_data);

    /* --- Bluetooth init --- */
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return;
    }

    printk("Bluetooth initialized\n");

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Advertising as \"%s\". Press SW1 to send notifications.\n",
           CONFIG_BT_DEVICE_NAME);

    /* Nothing else to do – callbacks & interrupts handle everything */
    for (;;) {
        k_sleep(K_SECONDS(1));
    }
}
