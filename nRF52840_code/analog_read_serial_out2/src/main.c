#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>

/* ---- ADC devicetree plumbing (uses zephyr,user io-channels) ---- */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified (need zephyr,user io-channels)"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)
};

#if DT_PROP_LEN(DT_PATH(zephyr_user), io_channels) < 2
#error "Need at least 2 ADC channels in zephyr,user io-channels"
#endif

/* ---- BLE UUIDs (128-bit custom) ---- */
static struct bt_uuid_128 adc_svc_uuid =
	BT_UUID_INIT_128(0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

static struct bt_uuid_128 adc1_mv_char_uuid =
	BT_UUID_INIT_128(0x11, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

static struct bt_uuid_128 adc2_mv_char_uuid =
	BT_UUID_INIT_128(0x12, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

/* ---- Latest values ---- */
static int16_t latest_mv_1 = 0;
static int16_t latest_mv_2 = 0;

/* Separate notify enables for each characteristic */
static bool notify_enabled_1 = false;
static bool notify_enabled_2 = false;

/* ---- Read callbacks ---- */
static ssize_t read_mv_1(struct bt_conn *conn,
			 const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &latest_mv_1, sizeof(latest_mv_1));
}

static ssize_t read_mv_2(struct bt_conn *conn,
			 const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &latest_mv_2, sizeof(latest_mv_2));
}

/* ---- CCC changed callbacks ---- */
static void mv1_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled_1 = (value == BT_GATT_CCC_NOTIFY);
	printk("Sensor 1 notifications %s\n", notify_enabled_1 ? "ENABLED" : "DISABLED");
}

static void mv2_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled_2 = (value == BT_GATT_CCC_NOTIFY);
	printk("Sensor 2 notifications %s\n", notify_enabled_2 ? "ENABLED" : "DISABLED");
}

/* ---- GATT service ---- */
BT_GATT_SERVICE_DEFINE(adc_svc,
	BT_GATT_PRIMARY_SERVICE(&adc_svc_uuid),

	/* Sensor 1 characteristic */
	BT_GATT_CHARACTERISTIC(&adc1_mv_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_mv_1, NULL, &latest_mv_1),
	BT_GATT_CCC(mv1_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	/* Sensor 2 characteristic */
	BT_GATT_CHARACTERISTIC(&adc2_mv_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_mv_2, NULL, &latest_mv_2),
	BT_GATT_CCC(mv2_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* Advertising payload */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		(sizeof(CONFIG_BT_DEVICE_NAME) - 1)),
};

static int read_one_adc_channel(const struct adc_dt_spec *ch, int16_t *out_sample, int16_t *out_mv)
{
	int err;
	int16_t sample = 0;

	struct adc_sequence sequence = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};

	adc_sequence_init_dt(ch, &sequence);

	err = adc_read_dt(ch, &sequence);
	if (err) {
		return err;
	}

	*out_sample = sample;

	int32_t mv32 = sample;
	err = adc_raw_to_millivolts_dt(ch, &mv32);
	if (err == 0) {
		*out_mv = (int16_t)mv32;

                
	} else {
		/* fallback to raw if mv conversion not available */
		*out_mv = sample;
	}

	return 0;
}

int main(void)
{
	int err;

	printk("Starting 2x ADC -> BLE notify example\n");

	/* ---- ADC init ---- */
	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!adc_is_ready_dt(&adc_channels[i])) {
			printk("ADC %zu (%s) not ready\n", i, adc_channels[i].dev->name);
			return 0;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err) {
			printk("ADC channel %zu setup failed (%d)\n", i, err);
			return 0;
		}
	}

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

	while (1) {
		int16_t raw1 = 0, raw2 = 0;

		err = read_one_adc_channel(&adc_channels[0], &raw1, &latest_mv_1);
		if (err) {
			printk("adc_read_dt ch0 failed (%d)\n", err);
			k_sleep(K_MSEC(250));
			continue;
		}

		err = read_one_adc_channel(&adc_channels[1], &raw2, &latest_mv_2);
		if (err) {
			printk("adc_read_dt ch1 failed (%d)\n", err);
			k_sleep(K_MSEC(250));
			continue;
		}

		printk("S1 raw=%d mv=%d notify=%d | S2 raw=%d mv=%d notify=%d\n",
		       raw1, latest_mv_1, notify_enabled_1,
		       raw2, latest_mv_2, notify_enabled_2);

		/*
		 * Attribute layout now is:
		 * attrs[0] = primary service
		 * attrs[1] = char declaration for sensor 1
		 * attrs[2] = char value for sensor 1
		 * attrs[3] = CCC for sensor 1
		 * attrs[4] = char declaration for sensor 2
		 * attrs[5] = char value for sensor 2
		 * attrs[6] = CCC for sensor 2
		 */
		if (notify_enabled_1) {
			(void)bt_gatt_notify(NULL, &adc_svc.attrs[2],
					     &latest_mv_1, sizeof(latest_mv_1));
		}

		if (notify_enabled_2) {
			(void)bt_gatt_notify(NULL, &adc_svc.attrs[5],
					     &latest_mv_2, sizeof(latest_mv_2));
		}

		k_sleep(K_MSEC(80));
	}

	return 0;
}