#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>

/* ---- ADC devicetree plumbing (uses your zephyr,user io-channels) ---- */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified (need zephyr,user io-channels)"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)
};

/* ---- BLE UUIDs (128-bit custom) ---- */
static struct bt_uuid_128 adc_svc_uuid =
	BT_UUID_INIT_128(0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

static struct bt_uuid_128 adc_mv_char_uuid =
	BT_UUID_INIT_128(0x11, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

/* We'll expose millivolts as a signed 16-bit value */
static int16_t latest_mv = 0;
static bool notify_enabled = false;

/* Read callback */
static ssize_t read_mv(struct bt_conn *conn,
		       const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len, uint16_t offset)
{
	/* Return the most recent value */
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &latest_mv, sizeof(latest_mv));
}

/* CCC changed callback */
static void mv_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Notifications %s\n", notify_enabled ? "ENABLED" : "DISABLED");
}

/* GATT service */
BT_GATT_SERVICE_DEFINE(adc_svc,
	BT_GATT_PRIMARY_SERVICE(&adc_svc_uuid),

	BT_GATT_CHARACTERISTIC(&adc_mv_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_mv, NULL, &latest_mv),

	BT_GATT_CCC(mv_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* Advertising payload: flags + device name (name comes from CONFIG_BT_DEVICE_NAME) */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		(sizeof(CONFIG_BT_DEVICE_NAME) - 1)),
};

int main(void)
{
	int err;

	printk("Starting ADC -> BLE notify example\n");

	/* ---- ADC init ---- */
	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!adc_is_ready_dt(&adc_channels[i])) {
			printk("ADC %s not ready\n", adc_channels[i].dev->name);
			return 0;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err) {
			printk("ADC channel setup failed (%d)\n", err);
			return 0;
		}
	}

	/* We'll read one channel (index 0) into this buffer */
	int16_t sample = 0;
	struct adc_sequence sequence = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};

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

	/* ---- Main loop: read ADC, convert to mV, notify if enabled ---- */
	while (1) {
		const struct adc_dt_spec *ch = &adc_channels[0];

		adc_sequence_init_dt(ch, &sequence);

		err = adc_read_dt(ch, &sequence);
		if (err) {
			printk("adc_read_dt failed!! (%d)\n", err);
			k_sleep(K_MSEC(250));
			continue;
		}

		/* Convert RAW -> mV using Zephyr helper */
		int32_t mv32 = sample;
		err = adc_raw_to_millivolts_dt(ch, &mv32);
		if (err == 0) {
			latest_mv = (int16_t)mv32;
		} else {
			/* If conversion unavailable, just send raw */
			latest_mv = sample;
		}

		/* Serial print for sanity */
		printk("raw=%d  mv=%d  notify=%d\n", sample, latest_mv, notify_enabled);

		if (notify_enabled) {
			/*
			 * IMPORTANT: notify the *value* attribute.
			 * In this service layout:
			 *   attrs[0] = primary service
			 *   attrs[1] = characteristic declaration
			 *   attrs[2] = characteristic value  <-- use this
			 *   attrs[3] = CCC
			 */
			(void)bt_gatt_notify(NULL, &adc_svc.attrs[2],
					     &latest_mv, sizeof(latest_mv));
		}

		k_sleep(K_MSEC(80));
	}

	return 0;
}
