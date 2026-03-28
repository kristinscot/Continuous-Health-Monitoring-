#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>

/* ---- BLE UUIDs (128-bit custom) ---- */
static struct bt_uuid_128 test_svc_uuid =
	BT_UUID_INIT_128(0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

static struct bt_uuid_128 counter_char_uuid =
	BT_UUID_INIT_128(0x11, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

/* Use unsigned 16-bit so 0..32000 fits naturally */
static uint16_t counter_value = 0;
static bool notify_enabled = false;

/* Read callback */
static ssize_t read_counter(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr,
			    void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &counter_value, sizeof(counter_value));
}

/* CCC changed callback */
static void counter_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Notifications %s\n", notify_enabled ? "ENABLED" : "DISABLED");
}

/* GATT service */
BT_GATT_SERVICE_DEFINE(test_svc,
	BT_GATT_PRIMARY_SERVICE(&test_svc_uuid),

	BT_GATT_CHARACTERISTIC(&counter_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_counter, NULL, &counter_value),

	BT_GATT_CCC(counter_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* Advertising payload */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		(sizeof(CONFIG_BT_DEVICE_NAME) - 1)),
};

int main(void)
{
	int err;
	int last_timestamp = k_uptime_get();
	printk("Starting BLE counter notify test\n");

	/* ---- BLE init ---- */
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
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}
	printk("Advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);

	/* ---- Main loop: send 0,1,2,...,32000 then wrap ---- */
	while (1) {
		/* Print to UART for sanity */
		if ((counter_value % 1000) == 0) {
			int now = k_uptime_get();
		printk("time since last=%d ms\n", now - last_timestamp);
		last_timestamp = now;
		}


		//Transmit Logic
		if (notify_enabled) {
			/*
			 * attrs[0] = primary service
			 * attrs[1] = characteristic declaration
			 * attrs[2] = characteristic value  <-- notify this
			 * attrs[3] = CCC
			 */
			err = bt_gatt_notify(NULL, &test_svc.attrs[2],
					     &counter_value, sizeof(counter_value));
			if (err) {
				printk("bt_gatt_notify failed (err %d)\n", err);
			}
		}

		counter_value++;
		if (counter_value > 3000) {
			counter_value = 0;
		}

		/* Adjust this to test throughput / packet loss */
		k_sleep(K_USEC(50));
	}

	return 0;
}