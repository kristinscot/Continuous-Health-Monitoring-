#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>  //for uart
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>  //for button
#include <zephyr/sys/util.h>
#include <inttypes.h>


//for bluetooth
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#ifdef CONFIG_ADC
#include <zephyr/drivers/adc.h>
#endif
#include <zephyr/sys/byteorder.h>

//button gpio container 
#define BUTTON0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS(BUTTON0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(BUTTON0_NODE, gpios,
							      {0});


//uart configuration
const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
const struct uart_config uart_cfg = {
		.baudrate = 115200,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE
};


//definitions for bluetooth
char ble_uart_buffer[150] = {0}; //buffer to transmit bluetooth specific messages over uart
volatile uint8_t notify_client = 0; //flag for notification enable/disable status
#define CONFIG_BT_DEVICE_NAME "Test Device"
#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1) 
#define NRF52_SERVICE_UUID         0xd4, 0x86, 0x48, 0x24, 0x54, 0xB3, 0x43, 0xA1, \
			           0xBC, 0x20, 0x97, 0x8F, 0xC3, 0x76, 0xC2, 0x75     //online uuid generator
#define NRF52_CHARACTERISTIC_UUID  0xED, 0xAA, 0x20, 0x11, 0x92, 0xE7, 0x43, 0x5A, \
			           0xAA, 0xE9, 0x94, 0x43, 0x35, 0x6A, 0xD4, 0xD3
#define BT_UUID_MY_SERVICE         BT_UUID_DECLARE_128(NRF52_SERVICE_UUID) //pointer to above service uuid
#define BT_UUID_MY_CHARACTERISTIC  BT_UUID_DECLARE_128(NRF52_CHARACTERISTIC_UUID) //pointer to above characteristic uuid


#ifdef CONFIG_ADC
/* --- EMG / ADC sampling configuration --- */
static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
#define ADC_CHANNEL_ID 0
#define ADC_RESOLUTION 12
#define SAMPLE_RATE_HZ 1000 /* Hz: change as needed */
#define BATCH_SIZE 20       /* number of samples per BLE packet */

static int16_t sample_batch[BATCH_SIZE];
static size_t batch_idx = 0;
static uint32_t packet_seq = 0;

static int16_t single_sample_buffer;
static struct adc_sequence sequence = {
    .channels = BIT(ADC_CHANNEL_ID),
    .buffer = &single_sample_buffer,
    .buffer_size = sizeof(single_sample_buffer),
    .resolution = ADC_RESOLUTION,
};

static void sampling_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (1) {
        int rc = adc_read(adc_dev, &sequence);
        if (rc == 0) {
            sample_batch[batch_idx++] = single_sample_buffer;
            if (batch_idx >= BATCH_SIZE) {
                if (my_connection && notify_client) {
                    uint8_t packet[3 + 4 + 2 + (BATCH_SIZE * 2)];
                    uint8_t *p = packet;
                    memcpy(p, "EMG", 3); p += 3; /* simple header */
                    uint32_t seq_le = sys_cpu_to_le32(packet_seq++);
                    memcpy(p, &seq_le, 4); p += 4;
                    uint16_t count_le = sys_cpu_to_le16(BATCH_SIZE);
                    memcpy(p, &count_le, 2); p += 2;
                    for (size_t i = 0; i < BATCH_SIZE; i++) {
                        int16_t s_le = sys_cpu_to_le16(sample_batch[i]);
                        memcpy(p, &s_le, 2); p += 2;
                    }

                    send_notification(my_connection, (const char *)packet, p - packet);
                }
                batch_idx = 0;
            }
        } else {
            nrf52_uart_tx("ADC read error\n");
        }

        /* Sleep to achieve approx SAMPLE_RATE_HZ */
        k_msleep(1000 / SAMPLE_RATE_HZ);
    }
}

K_THREAD_DEFINE(sampling_tid, 1024, sampling_thread, NULL, NULL, NULL, 7, 0, 0);
#endif /* CONFIG_ADC */

static K_SEM_DEFINE(ble_init_ok, 0, 1); //semaphore for ble initialization

//ble device advertisement (GAP)
static const struct bt_data ad[] = //advertising data setup
{
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)), 
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};
static const struct bt_data sd[] = //scan response data setup
{
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, NRF52_SERVICE_UUID),
};

struct bt_conn *my_connection; //bluetooth connection reference struct

//ble cccd update function declaration
void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value);

//register ble service  (GATT)
BT_GATT_SERVICE_DEFINE(my_service,
BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_SERVICE),      //Service Setup
BT_GATT_CHARACTERISTIC(BT_UUID_MY_CHARACTERISTIC, //Characteristic setup
                        BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
                        BT_GATT_PERM_READ,
                        NULL, NULL, NULL),
BT_GATT_CCC(on_cccd_changed,  //Client Characteristic Configuration Descriptor - Enable/Disable of Notification
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);


/*
 *@brief : uart tranmsit function
 *@param : buffer to be transmitted
 *@retval : None 
 *@note : function to transmit uart messages using blocking poll out method
*/
void nrf52_uart_tx(uint8_t *tx_buff){
	for(int buflen=0;buflen<=strlen(tx_buff);buflen++){
                uart_poll_out(uart_dev,*(tx_buff+buflen));
	}
	k_msleep(100);
}

/*
 *@brief : bluetooth notify cccd callback function
 *@param : gatt attribute structure , client characteristic configuration Values
 *@retval : None 
 *@note : function is called whenever the CCCD value has been changed by the client
*/
void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    switch(value)
    {
        case BT_GATT_CCC_NOTIFY: 
            // Start sending stuff! No ACK
			notify_client = 1;  //notify data send start
			nrf52_uart_tx("notify enabled\n");
            break;

        case BT_GATT_CCC_INDICATE: 
            // Start sending stuff via indications. Need ACK
			nrf52_uart_tx("indicate enabled\n");
            break;

        case 0: 
            // Stop sending stuff
			notify_client = 0;  //notify data send stop
			nrf52_uart_tx("notify/indicate disabled\n");
            break;
        
        default: 
            //Error, CCCD set to an invalid value    
			nrf52_uart_tx("invalid state\n");
    }
}   

/*
 *@brief : bluetooth notification callback function
 *@param : bluetooth connection structure 
 *@retval : None 
 *@note : function is called whenever a Notification has been sent by the Characteristic
*/
static void on_sent(struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(user_data);
        const bt_addr_le_t * addr = bt_conn_get_dst(conn);
    
	memset(ble_uart_buffer,0,sizeof(ble_uart_buffer));
	sprintf(ble_uart_buffer,"Data sent to Address 0x %02X %02X %02X %02X %02X %02X \n", addr->a.val[0]
                                                                    , addr->a.val[1]
                                                                    , addr->a.val[2]
                                                                    , addr->a.val[3]
                                                                    , addr->a.val[4]
                                                                    , addr->a.val[5]);
        nrf52_uart_tx(ble_uart_buffer);
}


/*
 *@brief : bluetooth notification callback function
 *@param : bluetooth connection structure 
 *@retval : None 
 *@note : This function sends a notification to a Client with the provided data,
        given that the Client Characteristic Control Descripter has been set to Notify (0x1).
        It also calls the on_sent() callback if successful
*/
void send_notification(struct bt_conn *conn, const char *data, uint16_t len)
{
    /* 
    The attribute for the characteristic is used with bt_gatt_is_subscribed 
    to check whether notification has been enabled by the peer or not.
    Attribute table: 0 = Service, 1 = Primary service, 2 = Characteristic, 3 = CCCD.
    */
    const struct bt_gatt_attr *attr = &my_service.attrs[2]; 

    struct bt_gatt_notify_params params = 
    {
        .uuid   = BT_UUID_MY_CHARACTERISTIC,
        .attr   = attr,
        .data   = data,
        .len    = len,
        .func   = on_sent
    };

    // Check whether notifications are enabled or not
    if(bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY)) 
    {
        // Send the notification
	    if(bt_gatt_notify_cb(conn, &params))
        {
            nrf52_uart_tx("Error, unable to send notification\n");
        }
    }
    else
    {
        nrf52_uart_tx("Warning, notification not enabled\n");
    }
}


/*
 *@brief : bluetooth connection callback function
 *@param : bluetooth connection structure , error status
 *@retval : None 
 *@note : This function is called whenever the device is connected with a Central device
*/
static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info; 
	char addr[BT_ADDR_LE_STR_LEN];

	my_connection = conn;

	if(bt_conn_get_info(conn, &info)!=0)
	{
		nrf52_uart_tx("connection info unavailable\n");
		return;
	}
	else
	{
		
		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
		memset(ble_uart_buffer,0,sizeof(ble_uart_buffer));
		sprintf(ble_uart_buffer,"Connected to: %s					\n\
			Role: %u							\n\
			Conn interval: %u				\n\
			Slv latency: %u					\n\
			Conn timeout: %u	\n"
			, addr, info.role, info.le.interval, info.le.latency, info.le.timeout
		);
		nrf52_uart_tx(ble_uart_buffer);
		k_msleep(200);

		/* Request larger MTU for higher throughput; result is asynchronous */
		int mtu_err = bt_gatt_exchange_mtu(conn, 247);
		if (mtu_err) {
			nrf52_uart_tx("MTU exchange request failed\n");
		}
	}
} 


/*
 *@brief : bluetooth disconnection callback function
 *@param : bluetooth connection structure , reason for disconnection
 *@retval : None 
 *@note :  This function is called whenever the device is disconnected from a Central device
*/
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	memset(ble_uart_buffer,0,sizeof(ble_uart_buffer));
	sprintf(ble_uart_buffer,"Disconnected (reason %u)\n", reason);
	nrf52_uart_tx(ble_uart_buffer);
}

//structure for connection based callbacks
static struct bt_conn_cb conn_callbacks = 
{
	.connected			= connected,
	.disconnected   		= disconnected,
};

/*
 *@brief : Callback for notifying that Bluetooth has been enabled.
 *@param : error condition (zero on success or (negative) error code otherwise.)
 *@retval : None 
*/
static void bt_ready(int err)
{
	if (err) 
	{
		memset(ble_uart_buffer,0,sizeof(ble_uart_buffer));
		sprintf(ble_uart_buffer,"BLE init failed with error code %d\n", err);
		nrf52_uart_tx(ble_uart_buffer);
		return;
	}

	//Configure connection callbacks
	bt_conn_cb_register(&conn_callbacks);

	//Start advertising
	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) 
	{
		memset(ble_uart_buffer,0,sizeof(ble_uart_buffer));
		sprintf(ble_uart_buffer,"Advertising failed to start (err %d)\n", err);
		nrf52_uart_tx(ble_uart_buffer);
		return;
	}

	nrf52_uart_tx("Advertising started!\n");

	k_sem_give(&ble_init_ok); //give semaphore
}


/*
 *@brief : main function
 *@param : execution status
 *@retval : None 
*/
int main(void)
{
        int err = 0;


#ifdef CONFIG_ADC
        /* Check ADC device */
        if (!device_is_ready(adc_dev)) {
                nrf52_uart_tx("ADC device not ready\n");
                return 0;
        }

        /* Configure ADC channel (adjust gain/reference/channel_id as required for your hardware) */
        struct adc_channel_cfg ch_cfg = {
                .gain = ADC_GAIN_1_6,
                .reference = ADC_REF_INTERNAL,
                .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10),
                .channel_id = ADC_CHANNEL_ID,
                .differential = 0,
        };

        int rc = adc_channel_setup(adc_dev, &ch_cfg);
        if (rc) {
                nrf52_uart_tx("ADC channel setup failed\n");
                return 0;
        }
#else
        nrf52_uart_tx("CONFIG_ADC not enabled, EMG sampling disabled\n");
#endif

	    
        /******************************************************************** 
        ***************************Button Section****************************
        ********************************************************************/
        /********************************************************************* 
        *************************Bluetooth Section****************************
        *********************************************************************/
        
        //Enable Bluetooth
        err = bt_enable(bt_ready);
        if (err) 
	{
		//Bluetooth Enabling failed
		nrf52_uart_tx("bluetooth enable error\n"); 
	}

         	
        //Bluetooth stack should be ready in less than 100 msec. 								
        //We use this semaphore to wait for bt_enable to call bt_ready.
        //This task/thread is blocked for the time period or until semaphore is given back.... 	

        err = k_sem_take(&ble_init_ok, K_MSEC(500));
        if (!err) 
	{
		//Bluetooth initialized
	} else 
	{
                //Bluetooth initialization did not complete in Time
		nrf52_uart_tx("bluetooth init timeout\n");
	}


        /********************************************************************* 
        ****************************Uart Section******************************
        *********************************************************************/
        
        if (!device_is_ready(uart_dev)) {  //check if device ready
                return 0;
        }

        err = uart_configure(uart_dev, &uart_cfg);  //set cfg.
        if (err == -ENOSYS) {
                return -ENOSYS;
        }


        nrf52_uart_tx("EMG sampling + BLE service started\n");

        while (1) {
                k_sleep(K_SECONDS(1));
        }

        return 0;
}