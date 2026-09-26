#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>


static const struct device *gpio_ct_dev= DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* ============================================================
 * UUID DEFINITIONS
 * ============================================================ */

/* Service UUID
 * f7547938-68ba-11ec-90d6-0242ac120003
 */
#define LED_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0xf7547938, 0x68ba, 0x11ec, \
			   0x90d6, 0x0242ac120003)

/* Temperature / Sensor Characteristic
 * 9c85a726-b7f1-11ec-b909-0242ac120002
 */
#define LED_CHARACTERISTIC_UUID_VAL \
	BT_UUID_128_ENCODE(0x9c85a726, 0xb7f1, 0x11ec, \
			   0xb909, 0x0242ac120002)

/* Control Characteristic
 * 9c85a727-b7f1-11ec-b909-0242ac120002
 */
#define BT_UUID_CONTROL_CHAR_VAL \
	BT_UUID_128_ENCODE(0x9c85a727, 0xb7f1, 0x11ec, \
			   0xb909, 0x0242ac120002)


static struct bt_uuid_128 led_svc_uuid =
	BT_UUID_INIT_128(LED_SERVICE_UUID_VAL);

static struct bt_uuid_128 led_state_char_uuid =
	BT_UUID_INIT_128(LED_CHARACTERISTIC_UUID_VAL);

static struct bt_uuid_128 control_char_uuid =
	BT_UUID_INIT_128(BT_UUID_CONTROL_CHAR_VAL);


/* ============================================================
 * GLOBAL VARIABLES
 * ============================================================ */

static struct bt_gatt_indicate_params ind_params;

static uint8_t indicating;


/* ============================================================
 * ADVERTISING DATA
 * ============================================================ */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,(BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,LED_SERVICE_UUID_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE,CONFIG_BT_DEVICE_NAME,sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};


/* ============================================================
 * TEMPERATURE CHARACTERISTIC CCC CALLBACK
 * ============================================================ */

static void htmc_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	if (value == BT_GATT_CCC_INDICATE) {
		printk("Temperature indication ENABLED\n");
	} else {
		printk("Temperature indication DISABLED\n");
	}
}


/* ============================================================
 * INDICATION CALLBACK
 * ============================================================ */

static void indicate_cb(struct bt_conn *conn,
			struct bt_gatt_indicate_params *params,
			uint8_t err)
{
	printk("Temperature indication %s\n",
	       err != 0U ? "failed" : "successful");
}


static void indicate_destroy(struct bt_gatt_indicate_params *params)
{
	printk("Indication complete\n");
	indicating = 0U;
}


/* ============================================================
 * CONTROL CHARACTERISTIC WRITE CALLBACK
 *
 * Flutter sends:
 *
 * [0] = Device ID
 * [1] = State
 *
 * 0x01 = Bulb
 * 0x02 = Fan
 *
 * State:
 * 0x00 = OFF
 * 0x01 = ON
 * ============================================================ */

static ssize_t write_control_cb(struct bt_conn *conn,const struct bt_gatt_attr *attr,const void *buf,
				uint16_t len,uint16_t offset,uint8_t flags)
{
	const uint8_t *data = buf;

	/* We only support writing from offset 0 */
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	/* Flutter sends exactly 2 bytes */
	if (len != 2) {
		printk("Invalid control command length: %u\n", len);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	uint8_t device_id = data[0];
	uint8_t state = data[1];

	printk("Control command received:\n");
	printk("  Device ID = 0x%02X\n", device_id);
	printk("  State     = 0x%02X\n", state);


	/* State must be 0 or 1 */
	if (state > 1) {
		printk("Invalid state\n");
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}


	switch (device_id) {

	case 0x01:
		/* Bulb */
		if (state) {
			printk("Bulb ON\n");
			//gpio_pin_toggle_dt(&led0);
			gpio_pin_set_raw(gpio_ct_dev,2,1);

		} else {
			printk("Bulb OFF\n");
			gpio_pin_set_raw(gpio_ct_dev,2,0);
		}
		break;

	case 0x02:
		/* Fan */
		if (state) {
			printk("Fan ON\n");
			gpio_pin_set_raw(gpio_ct_dev,4,1); 
		} else {
			printk("Fan OFF\n");
			gpio_pin_set_raw(gpio_ct_dev,4,0); 
		}
		break;

	default:
		printk("Unknown device ID: 0x%02X\n",device_id);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}


	return len;
}


/* ============================================================
 * GATT SERVICE
 *
 * One service containing:
 *
 * 1. Temperature characteristic
 *    -> INDICATE
 *
 * 2. Control characteristic
 *    -> READ + WRITE
 *
 * ============================================================ */

BT_GATT_SERVICE_DEFINE(smart_control_svc,

	/* Primary Service -  attrs[0]  */
	BT_GATT_PRIMARY_SERVICE(&led_svc_uuid),


	/* ========================================================
	 * Temperature Characteristic  - 
	 * ======================================================== */

	BT_GATT_CHARACTERISTIC(
		&led_state_char_uuid.uuid,
		BT_GATT_CHRC_INDICATE,
		BT_GATT_PERM_NONE,
		NULL,
		NULL,
		NULL
	),

	/* CCCD for temperature indication  -  atts[3] */
	BT_GATT_CCC(
		htmc_ccc_cfg_changed,
		BT_GATT_PERM_READ |
		BT_GATT_PERM_WRITE
	),


	/* ========================================================
	 * Control Characteristic  -  arrts[4] decleration
	 *                            atts[5] value (write_control_cb)
	 *
	 * Flutter writes:
	 *
	 * [0x01, 0x01] -> Bulb ON
	 * [0x01, 0x00] -> Bulb OFF
	 *
	 * [0x02, 0x01] -> Fan ON
	 * [0x02, 0x00] -> Fan OFF
	 * ======================================================== */

	BT_GATT_CHARACTERISTIC(
		&control_char_uuid.uuid,
		BT_GATT_CHRC_READ |
		BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ |
		BT_GATT_PERM_WRITE,
		NULL,
		write_control_cb,
		NULL
	)
);


/* ============================================================
 * CONNECTION CALLBACKS
 * ============================================================ */

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n",err,bt_hci_err_to_str(err));
	} else {
		printk("Connected\n");
	}
}


static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n",reason,bt_hci_err_to_str(reason));
}


/* ============================================================
 * BLUETOOTH READY
 * ============================================================ */

static void bt_ready(void)
{
	int err;
	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,ad,ARRAY_SIZE(ad),sd,ARRAY_SIZE(sd));

	if (err) {
		printk("Advertising failed to start ""(err %d)\n", err);
		return;
	}
	printk("Advertising successfully started\n");
}



BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = bt_ready,
};





/* ============================================================
 * RANDOM NUMBER
 * ============================================================ */

static int random_range(int min, int max)
{
	return min + (sys_rand32_get() % (max - min + 1));
}


/* ============================================================
 * MAIN
 * ============================================================ */

int main(void)
{
	int err;


	if (!device_is_ready(gpio_ct_dev)) {
		return 0;
	}

	gpio_pin_configure(gpio_ct_dev, 2, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure(gpio_ct_dev, 4, GPIO_OUTPUT_ACTIVE);

	err = bt_enable(NULL);

	if (err) {
		printk("Bluetooth init failed ""(err %d)\n", err);
		return 0;
	}

	
	bt_ready();

	/*Set LED as low staete*/
	gpio_pin_set_raw(gpio_ct_dev,2,0); 
	gpio_pin_set_raw(gpio_ct_dev,4,0); 

	/* ========================================================
	 * MAIN LOOP
	 * ======================================================== */

	while (1) {

			
		uint16_t temperature = random_range(20, 37) * 10;
		uint8_t humidity = random_range(10, 25);

		uint8_t htm[5] = {0};

		htm[0] = temperature >> 8;		/* Temperature high byte */
		htm[1] = temperature & 0xFF;	/* Temperature low byte */
		htm[4] = humidity;				/* Humidity */

		printk("Temperature: %u.%u C\n",temperature / 10,temperature % 10);

		printk("Humidity: %u.%u %%\n",humidity / 10,humidity % 10);


		/* ----------------------------------------------------
		 * Send indication
		 * ---------------------------------------------------- */

		if (!indicating) {
			memset(&ind_params, 0,sizeof(ind_params));

			/*
			 * Attribute layout:
			 *
			 * attrs[0] = Primary Service
			 * attrs[1] = Temperature Characteristic Declaration
			 * attrs[2] = Temperature Characteristic Value
			 * attrs[3] = Temperature CCCD
			 * attrs[4] = Control Characteristic Declaration
			 * attrs[5] = Control Characteristic Value
			 */

			ind_params.attr = &smart_control_svc.attrs[2];
			ind_params.func = indicate_cb;
			ind_params.destroy =indicate_destroy;
			ind_params.data = htm;
			ind_params.len = sizeof(htm);

			err = bt_gatt_indicate(NULL,&ind_params);

			if (err == 0) {
				indicating = 1U;
			} else {
				printk("Indication failed: %d\n",err);
			}
		}

		k_sleep(K_SECONDS(1));

	}

	return 0;
}