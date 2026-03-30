#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>

/* ---- BLE UUIDs ---- */

static struct bt_uuid_128 test_svc_uuid =
BT_UUID_INIT_128(0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
                 0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe);

static struct bt_uuid_128 counter_char_uuid =
BT_UUID_INIT_128(0x11,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
                 0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe);

/* ---- Data ---- */

#define BLOCK_SIZE 80

static uint16_t counter_block[BLOCK_SIZE];
static uint16_t counter = 0;

static bool notify_enabled = false;

/* ---- Read callback ---- */

static ssize_t read_counter(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             counter_block, sizeof(counter_block));
}

/* ---- CCC callback ---- */

static void counter_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("Notifications %s\n", notify_enabled ? "ENABLED" : "DISABLED");
}

/* ---- Service ---- */

BT_GATT_SERVICE_DEFINE(test_svc,
    BT_GATT_PRIMARY_SERVICE(&test_svc_uuid),

    BT_GATT_CHARACTERISTIC(&counter_char_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_counter, NULL, counter_block),

    BT_GATT_CCC(counter_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* ---- Advertising ---- */

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            (sizeof(CONFIG_BT_DEVICE_NAME) - 1)),
};

/* ---- Main ---- */

int main(void)
{
    int err;

    printk("Starting BLE counter block test\n");

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("Bluetooth initialized\n");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2,
                          ad, ARRAY_SIZE(ad),
                          NULL, 0);
    if (err) {
        printk("Advertising failed (err %d)\n", err);
        return 0;
    }

    printk("Advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);

    while (1) {

        /* Fill block */

        for (int i = 0; i < BLOCK_SIZE; i++) {

            counter_block[i] = counter;

            counter++;

            if (counter > 32000) {
                counter = 0;
            }
        }

        if (notify_enabled) {

            int err = bt_gatt_notify(NULL,
                                     &test_svc.attrs[2],
                                     counter_block,
                                     sizeof(counter_block));

            if (err) {
                printk("notify error %d\n", err);
            }
        }

        /* Optional debug print */

        if ((counter % 1000) == 0) {
            printk("counter=%u\n", counter);
        }

        /* No sleep = maximum throughput */
    }

    return 0;
}