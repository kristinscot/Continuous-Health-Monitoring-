#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/devicetree.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>

#include <nrfx_saadc.h>
#include <nrfx_timer.h>
#include <helpers/nrfx_gppi.h>

#include <hal/nrf_saadc.h>

#include <stdint.h>
#include <stdbool.h>

/* =========================
 * Settings
 * ========================= */

#define SAADC_SAMPLE_INTERVAL_US 31
#define SAADC_BUFFER_SIZE        256

/* Change if needed */
#define ADC_INPUT_PIN            NRF_SAADC_INPUT_AIN0

#define WARNING_DELTA_RAW        100
#define CRITICAL_DELTA_RAW       1000

#define TIMER_INSTANCE_NUMBER    2

/* =========================
 * BLE UUIDs / GATT
 * ======================== */

static struct bt_uuid_128 adc_svc_uuid =
	BT_UUID_INIT_128(0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

static struct bt_uuid_128 adc_mv_char_uuid =
	BT_UUID_INIT_128(0x11, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
			 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe);

static int16_t latest_mv = 0;
static bool notify_enabled = false;

/* =========================
 * SAADC / TIMER / GPPI
 * ========================= */

static nrfx_timer_t timer_instance = NRFX_TIMER_INSTANCE(TIMER_INSTANCE_NUMBER);

static nrfx_gppi_handle_t sample_handle;
static nrfx_gppi_handle_t start_handle;

static int16_t saadc_sample_buffer[2][SAADC_BUFFER_SIZE];
static uint32_t saadc_current_buffer = 0;

struct adc_summary {
	int16_t latest_raw;
	uint16_t max_abs_delta;
	uint32_t buffer_count;
};

static volatile struct adc_summary summary_data;
static volatile uint32_t summary_seq;
static volatile bool runtime_error;
static volatile int runtime_error_code;

/* =========================
 * BLE helpers
 * ========================= */

static ssize_t read_mv(struct bt_conn *conn,
		       const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &latest_mv, sizeof(latest_mv));
}

static void mv_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Notifications %s\n", notify_enabled ? "ENABLED" : "DISABLED");
}

BT_GATT_SERVICE_DEFINE(adc_svc,
	BT_GATT_PRIMARY_SERVICE(&adc_svc_uuid),
	BT_GATT_CHARACTERISTIC(&adc_mv_char_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_mv, NULL, &latest_mv),
	BT_GATT_CCC(mv_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		(sizeof(CONFIG_BT_DEVICE_NAME) - 1)),
};

/* =========================
 * Utility
 * ========================= */

static inline uint16_t abs_u16_diff(int16_t a, int16_t b)
{
	int32_t d = (int32_t)a - (int32_t)b;
	if (d < 0) {
		d = -d;
	}
	return (uint16_t)d;
}

/* 12-bit SAADC, gain 1/6, internal ref 0.6 V => 3.6 V full-scale */
static int16_t raw_to_mv(int16_t raw)
{
	if (raw < 0) {
		raw = 0;
	}
	if (raw > 4095) {
		raw = 4095;
	}

	return (int16_t)(((int32_t)raw * 3600) / 4095);
}

/* =========================
 * SAADC callback
 * ========================= */

static void saadc_event_handler(nrfx_saadc_evt_t const *p_event)
{
	if (p_event == NULL) {
		return;
	}

	switch (p_event->type) {
	case NRFX_SAADC_EVT_BUF_REQ: {
		nrfx_err_t err;

		err = nrfx_saadc_buffer_set(saadc_sample_buffer[saadc_current_buffer],
					    SAADC_BUFFER_SIZE);
		if (err != NRFX_SUCCESS) {
			runtime_error = true;
			runtime_error_code = err;
		}

		saadc_current_buffer = (saadc_current_buffer + 1U) % 2U;
		break;
	}

	case NRFX_SAADC_EVT_DONE: {
		int16_t *buf = p_event->data.done.p_buffer;
		uint16_t count = p_event->data.done.size;

		if ((buf == NULL) || (count == 0U)) {
			break;
		}

		uint16_t max_delta = 0;
		for (uint16_t i = 1; i < count; i++) {
			uint16_t d = abs_u16_diff(buf[i], buf[i - 1]);
			if (d > max_delta) {
				max_delta = d;
			}
		}

		unsigned int key = irq_lock();
		summary_data.latest_raw = buf[count - 1];
		summary_data.max_abs_delta = max_delta;
		summary_data.buffer_count++;
		summary_seq++;
		irq_unlock(key);
		break;
	}

	default:
		break;
	}
}

/* =========================
 * Init helpers
 * ========================= */

static int configure_timer(void)
{
	nrfx_err_t err;
	nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG(1000000);
	uint32_t timer_ticks;

	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;

	err = nrfx_timer_init(&timer_instance, &timer_config, NULL);
	if (err != NRFX_SUCCESS) {
		printk("nrfx_timer_init error: %08x\n", err);
		return -1;
	}

	timer_ticks = nrfx_timer_us_to_ticks(&timer_instance, SAADC_SAMPLE_INTERVAL_US);

	nrfx_timer_extended_compare(&timer_instance,
				    NRF_TIMER_CC_CHANNEL0,
				    timer_ticks,
				    NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
				    false);

	return 0;
}

static int configure_saadc(void)
{
	nrfx_err_t err;
	static nrfx_saadc_channel_t channel =
		NRFX_SAADC_DEFAULT_CHANNEL_SE(ADC_INPUT_PIN, 0);
	nrfx_saadc_adv_config_t saadc_adv_config = NRFX_SAADC_DEFAULT_ADV_CONFIG;

	IRQ_CONNECT(DT_IRQN(DT_NODELABEL(adc)),
		    DT_IRQ(DT_NODELABEL(adc), priority),
		    nrfx_isr, nrfx_saadc_irq_handler, 0);

	err = nrfx_saadc_init(DT_IRQ(DT_NODELABEL(adc), priority));
	if (err != NRFX_SUCCESS) {
		printk("nrfx_saadc_init error: %08x\n", err);
		return -1;
	}

	channel.channel_config.gain = NRF_SAADC_GAIN1_6;
	channel.channel_config.reference = NRF_SAADC_REFERENCE_INTERNAL;
	channel.channel_config.acq_time = NRF_SAADC_ACQTIME_10US;
	channel.channel_config.burst = NRF_SAADC_BURST_DISABLED;

	err = nrfx_saadc_channels_config(&channel, 1);
	if (err != NRFX_SUCCESS) {
		printk("nrfx_saadc_channels_config error: %08x\n", err);
		return -1;
	}

	err = nrfx_saadc_advanced_mode_set(BIT(0),
					   NRF_SAADC_RESOLUTION_12BIT,
					   &saadc_adv_config,
					   saadc_event_handler);
	if (err != NRFX_SUCCESS) {
		printk("nrfx_saadc_advanced_mode_set error: %08x\n", err);
		return -1;
	}

	err = nrfx_saadc_buffer_set(saadc_sample_buffer[0], SAADC_BUFFER_SIZE);
	if (err != NRFX_SUCCESS) {
		printk("nrfx_saadc_buffer_set(0) error: %08x\n", err);
		return -1;
	}

	saadc_current_buffer = 1;

	err = nrfx_saadc_buffer_set(saadc_sample_buffer[1], SAADC_BUFFER_SIZE);
	if (err != NRFX_SUCCESS) {
		printk("nrfx_saadc_buffer_set(1) error: %08x\n", err);
		return -1;
	}

	return 0;
}

static int configure_gppi(void)
{
	int err;
	uint32_t timer_compare_evt;
	uint32_t saadc_sample_task;
	uint32_t saadc_end_evt;
	uint32_t saadc_start_task;

	timer_compare_evt =
		nrfx_timer_compare_event_address_get(&timer_instance, NRF_TIMER_CC_CHANNEL0);

	saadc_sample_task =
		nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE);

	saadc_end_evt =
		nrf_saadc_event_address_get(NRF_SAADC, NRF_SAADC_EVENT_END);

	saadc_start_task =
		nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_START);

	err = nrfx_gppi_conn_alloc(timer_compare_evt, saadc_sample_task, &sample_handle);
	if (err != NRFX_SUCCESS) {
		printk("sample conn alloc error: %08x\n", err);
		return -1;
	}

	err = nrfx_gppi_conn_alloc(saadc_end_evt, saadc_start_task, &start_handle);
	if (err != NRFX_SUCCESS) {
		printk("start conn alloc error: %08x\n", err);
		return -1;
	}

	nrfx_gppi_conn_enable(sample_handle);
	nrfx_gppi_conn_enable(start_handle);

	return 0;
}

static int ble_init_and_advertise(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return err;
	}

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2,
			      ad, ARRAY_SIZE(ad),
			      NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return err;
	}

	printk("Advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);
	return 0;
}

/* =========================
 * Main
 * ========================= */

int main(void){
	int err;
	uint32_t last_seq_seen = 0;
	printk("boot\n");
	printk("Starting ADC TIMER/GPPI example\n");

	err = configure_timer();
	if (err) {
		return 0;
	}

	err = configure_saadc();
	if (err) {
		return 0;
	}

	err = configure_gppi();
	if (err) {
		return 0;
	}

	err = ble_init_and_advertise();
	if (err) {
		return 0;
	}

	err = nrfx_saadc_mode_trigger();
	if (err != NRFX_SUCCESS) {
		printk("nrfx_saadc_mode_trigger error: %08x\n", err);
		return 0;
	}

	nrfx_timer_enable(&timer_instance);

	while (1) {
		if (runtime_error) {
			printk("Runtime error: %08x\n", runtime_error_code);
			runtime_error = false;
		}

		if (summary_seq != last_seq_seen) {
			struct adc_summary local;
			unsigned int key = irq_lock();

			local = summary_data;
			last_seq_seen = summary_seq;

			irq_unlock(key);

			latest_mv = raw_to_mv(local.latest_raw);

			if (local.max_abs_delta > CRITICAL_DELTA_RAW) {
				printk("CRITICAL raw=%d mv=%d max_delta=%u buf=%u\n",
				       local.latest_raw,
				       latest_mv,
				       local.max_abs_delta,
				       local.buffer_count);
			} else if (local.max_abs_delta > WARNING_DELTA_RAW) {
				printk("WARNING raw=%d mv=%d max_delta=%u buf=%u\n",
				       local.latest_raw,
				       latest_mv,
				       local.max_abs_delta,
				       local.buffer_count);
			}

			if (notify_enabled) {
				(void)bt_gatt_notify(NULL, &adc_svc.attrs[2],
						     &latest_mv, sizeof(latest_mv));
			}
		}

		k_sleep(K_MSEC(5));
	}

	return 0;
}