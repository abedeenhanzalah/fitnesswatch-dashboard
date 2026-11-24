/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/display/cfb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <hal/nrf_power.h>
#include "telemetry.h"
#include "pulse.h"
#include "max30102.h"
#include "activity.h"
#include "oximeter.h"
#include "battery_level.h"
#include "display_cycle.h"
#include "iit_delhi_logo.h"
#include "aqi.h"
LOG_MODULE_REGISTER(watch, LOG_LEVEL_INF);
#define SERVICE_UUID BT_UUID_128_ENCODE(0x12345678,0x1234,0x5678,0x1234,0x56789abcdef0)
#define DATA_UUID BT_UUID_128_ENCODE(0xabcdef01,0x1234,0x5678,0x1234,0x56789abcdef0)
static struct bt_uuid_128 service_uuid=BT_UUID_INIT_128(SERVICE_UUID);
static struct bt_uuid_128 data_uuid=BT_UUID_INIT_128(DATA_UUID);
static const struct device *const bus=DEVICE_DT_GET(DT_NODELABEL(xiao_i2c));
static const struct device *const oled=DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const imu=DEVICE_DT_GET(DT_NODELABEL(mpu6050));
static const struct adc_dt_spec battery_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct gpio_dt_spec battery_enable =
 GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), read_enable_gpios);
static uint8_t packet[12]={1};
static uint8_t motion_packet[13];
static uint8_t raw_packet[WATCH_RAW_PACKET_SIZE] = {1};
static uint8_t activity_packet[WATCH_ACTIVITY_PACKET_SIZE] = {1};
static struct pulse_state pulse;
static struct watch_oximeter oxygen;
static struct watch_clock watch_time;
static struct watch_aqi air_quality;
static struct watch_screen_order screen_order = WATCH_DEFAULT_SCREEN_ORDER;
static int64_t screen_order_set_at;
static struct nvs_fs watch_storage;
#define DAILY_STEPS_RECORD_ID 0x5354U
#define SCREEN_ORDER_RECORD_ID 0x5343U
static struct bt_uuid_128 motion_uuid =
 BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xabcdef02,0x1234,0x5678,0x1234,0x56789abcdef0));
static struct bt_uuid_128 raw_uuid =
 BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xabcdef03,0x1234,0x5678,0x1234,0x56789abcdef0));
static struct bt_uuid_128 activity_uuid =
 BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xabcdef04,0x1234,0x5678,0x1234,0x56789abcdef0));
static struct bt_uuid_128 clock_uuid =
 BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xabcdef05,0x1234,0x5678,0x1234,0x56789abcdef0));
static struct bt_uuid_128 aqi_uuid =
 BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xabcdef06,0x1234,0x5678,0x1234,0x56789abcdef0));
static struct bt_uuid_128 screen_order_uuid =
 BT_UUID_INIT_128(BT_UUID_128_ENCODE(0xabcdef07,0x1234,0x5678,0x1234,0x56789abcdef0));
K_MUTEX_DEFINE(packet_lock);
static atomic_t linked;
static ssize_t read_data(struct bt_conn *conn,const struct bt_gatt_attr *attr,
                         void *buf,uint16_t len,uint16_t offset)
{
 uint8_t snapshot[12];
 k_mutex_lock(&packet_lock,K_FOREVER);
 memcpy(snapshot,packet,sizeof(snapshot));
 k_mutex_unlock(&packet_lock);
 return bt_gatt_attr_read(conn,attr,buf,len,offset,snapshot,sizeof(snapshot));
}
static ssize_t read_motion(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset)
{
 uint8_t snapshot[sizeof(motion_packet)];
 k_mutex_lock(&packet_lock, K_FOREVER);
 memcpy(snapshot, motion_packet, sizeof(snapshot));
 k_mutex_unlock(&packet_lock);
 return bt_gatt_attr_read(conn, attr, buf, len, offset, snapshot, sizeof(snapshot));
}
static ssize_t read_raw(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
 uint8_t snapshot[sizeof(raw_packet)];
 k_mutex_lock(&packet_lock, K_FOREVER);
 memcpy(snapshot, raw_packet, sizeof(snapshot));
 k_mutex_unlock(&packet_lock);
 return bt_gatt_attr_read(conn, attr, buf, len, offset, snapshot, sizeof(snapshot));
}
static void ccc_changed(const struct bt_gatt_attr *attr,uint16_t value)
{ LOG_INF("Telemetry subscription: %u",value); }
static ssize_t read_activity(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
 uint8_t snapshot[sizeof(activity_packet)];
 k_mutex_lock(&packet_lock, K_FOREVER);
 memcpy(snapshot, activity_packet, sizeof(snapshot));
 k_mutex_unlock(&packet_lock);
 return bt_gatt_attr_read(conn, attr, buf, len, offset, snapshot, sizeof(snapshot));
}
static ssize_t write_clock(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
 if (offset) return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
 if (len != WATCH_CLOCK_PACKET_SIZE) return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
 const uint8_t *data = buf;
 if (data[0] != 1) return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
 k_mutex_lock(&packet_lock, K_FOREVER);
 bool accepted = watch_clock_set(&watch_time, sys_get_le32(data + 1),
                                 (int16_t)sys_get_le16(data + 5), k_uptime_get());
 k_mutex_unlock(&packet_lock);
 return accepted ? len : BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
}
static ssize_t write_aqi(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
 if (offset) return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
 if (len != WATCH_AQI_PACKET_SIZE) return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
 k_mutex_lock(&packet_lock, K_FOREVER);
 bool accepted = watch_aqi_receive(&air_quality, buf, k_uptime_get());
 k_mutex_unlock(&packet_lock);
 return accepted ? len : BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
}
static ssize_t read_screen_order(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
 uint8_t data[WATCH_SCREEN_PACKET_MAX];
 k_mutex_lock(&packet_lock, K_FOREVER);
 size_t size = watch_screen_order_encode(&screen_order, data);
 k_mutex_unlock(&packet_lock);
 return bt_gatt_attr_read(conn, attr, buf, len, offset, data, size);
}

static ssize_t write_screen_order(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
 if (offset) return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
 struct watch_screen_order candidate;
 if (!watch_screen_order_decode(&candidate, buf, len)) {
  return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
 }
 k_mutex_lock(&packet_lock, K_FOREVER);
 if (memcmp(&screen_order, &candidate, sizeof(candidate)) != 0) {
  screen_order = candidate;
  screen_order_set_at = k_uptime_get();
 }
 k_mutex_unlock(&packet_lock);
 return len;
}
BT_GATT_SERVICE_DEFINE(watch_svc,
 BT_GATT_PRIMARY_SERVICE(&service_uuid),
 BT_GATT_CHARACTERISTIC(&data_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY,
                        BT_GATT_PERM_READ,read_data,NULL,NULL),
 BT_GATT_CCC(ccc_changed,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE),
 BT_GATT_CHARACTERISTIC(&motion_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY,
                        BT_GATT_PERM_READ,read_motion,NULL,NULL),
 BT_GATT_CCC(ccc_changed,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE),
 BT_GATT_CHARACTERISTIC(&raw_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY,
                        BT_GATT_PERM_READ,read_raw,NULL,NULL),
 BT_GATT_CCC(ccc_changed,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE),
 BT_GATT_CHARACTERISTIC(&activity_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY,
                        BT_GATT_PERM_READ,read_activity,NULL,NULL),
 BT_GATT_CCC(ccc_changed,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE),
 BT_GATT_CHARACTERISTIC(&clock_uuid.uuid,BT_GATT_CHRC_WRITE,
                        BT_GATT_PERM_WRITE,NULL,write_clock,NULL),
 BT_GATT_CHARACTERISTIC(&aqi_uuid.uuid,BT_GATT_CHRC_WRITE,
                        BT_GATT_PERM_WRITE,NULL,write_aqi,NULL),
 BT_GATT_CHARACTERISTIC(&screen_order_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE,
                        BT_GATT_PERM_READ|BT_GATT_PERM_WRITE,read_screen_order,write_screen_order,NULL));
static const struct bt_data ad[]={
 BT_DATA_BYTES(BT_DATA_FLAGS,BT_LE_AD_GENERAL|BT_LE_AD_NO_BREDR),
 BT_DATA_BYTES(BT_DATA_UUID128_ALL,SERVICE_UUID)};
static const struct bt_data sd[]={BT_DATA(BT_DATA_NAME_COMPLETE,"FitnessWatch",12)};
static void advertise(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(adv_work,advertise);
static void advertise(struct k_work *work)
{
 if (atomic_get(&linked)) return;
 int err=bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,ad,ARRAY_SIZE(ad),sd,ARRAY_SIZE(sd));
 if (err && err!=-EALREADY) {
  LOG_WRN("Advertising error %d; retrying",err);
  k_work_reschedule(&adv_work,K_SECONDS(2));
 }
}
static void connected(struct bt_conn *conn,uint8_t err)
{
 if (!err) { atomic_set(&linked,1); LOG_INF("BLE connected"); }
 else k_work_reschedule(&adv_work,K_SECONDS(1));
}
static void disconnected(struct bt_conn *conn,uint8_t reason)
{ atomic_clear(&linked); LOG_INF("BLE disconnected: %u",reason); }
static void recycled(void) { k_work_reschedule(&adv_work,K_NO_WAIT); }
BT_CONN_CB_DEFINE(connection_callbacks)={.connected=connected,
 .disconnected=disconnected,.recycled=recycled};
static int max30102_read(uint32_t *red, uint32_t *ir, uint32_t *sample_seq,
                        bool pulse_moving, bool oxygen_moving, unsigned cadence)
{
 struct max30102_sample samples[31];
 int count = max30102_read_fifo(bus, samples, ARRAY_SIZE(samples));
 if (count < 0) return count;
 for (int i = 0; i < count; ++i) {
  *red = samples[i].red;
  *ir = samples[i].ir;
  pulse_sample_motion(&pulse, *ir, pulse_moving, cadence);
  oximeter_sample(&oxygen, *red, *ir, oxygen_moving);
  printk("WATCH_PPG %u %u %u\n", (*sample_seq)++, *red, *ir);
 }
 return 0;
}

static int sample_imu(int32_t axes[6], int16_t *temperature, bool *temperature_ok)
{
 struct sensor_value readings[6];
 *temperature_ok = false;
 int err = device_is_ready(imu) ? sensor_sample_fetch(imu) : -ENODEV;
 if (!err) err = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, readings);
 if (!err) err = sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, readings + 3);
 if (err) return err;
 for (unsigned i = 0; i < ARRAY_SIZE(readings); ++i) {
  axes[i] = sensor_value_to_micro(&readings[i]) / 1000;
 }
 struct sensor_value die_temperature;
 if (sensor_channel_get(imu, SENSOR_CHAN_DIE_TEMP, &die_temperature) == 0) {
  *temperature = sensor_value_to_micro(&die_temperature) / 10000;
  *temperature_ok = true;
 }
 return 0;
}

static int read_battery_mv(void)
{
 int16_t raw;
 struct adc_sequence sequence = { .buffer = &raw, .buffer_size = sizeof(raw) };
 int err = adc_sequence_init_dt(&battery_adc, &sequence);
 if (err) return err;
 err = adc_read_dt(&battery_adc, &sequence);
 if (err) return err;
 int32_t divided_mv = raw;
 err = adc_raw_to_millivolts_dt(&battery_adc, &divided_mv);
 return err ? err : watch_battery_millivolts(divided_mv);
}

static void show_boot_logo(void)
{
 const int16_t left = (DT_PROP(DT_CHOSEN(zephyr_display), width) - IIT_DELHI_LOGO_WIDTH) / 2;
 const int16_t top = (DT_PROP(DT_CHOSEN(zephyr_display), height) - IIT_DELHI_LOGO_HEIGHT) / 2;
 cfb_framebuffer_clear(oled, false);
 for (int16_t y = 0; y < IIT_DELHI_LOGO_HEIGHT; y++) {
  for (int16_t x = 0; x < IIT_DELHI_LOGO_WIDTH; x++) {
   if (iit_delhi_logo[y][x / 8] & (0x80U >> (x % 8))) {
    const struct cfb_position pixel = { .x = left + x, .y = top + y };
    cfb_draw_point(oled, &pixel);
   }
  }
 }
 cfb_framebuffer_finalize(oled);
}

static int load_watch_settings(struct watch_daily_step_record *saved)
{
 watch_storage.flash_device = PARTITION_DEVICE(storage_partition);
 watch_storage.offset = PARTITION_OFFSET(storage_partition);
 if (!device_is_ready(watch_storage.flash_device)) return -ENODEV;

 struct flash_pages_info page;
 int err = flash_get_page_info_by_offs(watch_storage.flash_device, watch_storage.offset, &page);
 if (err) return err;
 watch_storage.sector_size = page.size;
 watch_storage.sector_count = PARTITION_SIZE(storage_partition) / page.size;
 err = nvs_mount(&watch_storage);
 if (err) return err;

 struct watch_daily_step_record record;
 ssize_t size = nvs_read(&watch_storage, DAILY_STEPS_RECORD_ID, &record, sizeof(record));
 if (size == sizeof(record)) *saved = record;
 uint8_t data[WATCH_SCREEN_PACKET_MAX];
 size = nvs_read(&watch_storage, SCREEN_ORDER_RECORD_ID, data, sizeof(data));
 if (size > 0) watch_screen_order_decode(&screen_order, data, size);
 return 0;
}

static void draw_battery(int percent)
{
 const struct cfb_position top_left = { .x = 74, .y = 4 };
 const struct cfb_position bottom_right = { .x = 85, .y = 11 };
 const struct cfb_position tip_top = { .x = 86, .y = 6 };
 const struct cfb_position tip_bottom = { .x = 86, .y = 9 };
 cfb_draw_rect(oled, &top_left, &bottom_right);
 cfb_draw_line(oled, &tip_top, &tip_bottom);

 int filled = watch_battery_fill(percent, 10);
 if (!filled) return;
 for (int16_t y = 5; y <= 10; y++) {
  const struct cfb_position start = { .x = 75, .y = y };
  const struct cfb_position end = { .x = 74 + filled, .y = y };
  cfb_draw_line(oled, &start, &end);
 }
}

static void draw_step_goal(uint32_t steps)
{
 const struct cfb_position top_left = { .x = 0, .y = 34 };
 const struct cfb_position bottom_right = { .x = 127, .y = 44 };
 cfb_draw_rect(oled, &top_left, &bottom_right);

 uint16_t filled = watch_step_goal_fill(steps, 124);
 if (!filled) return;
 for (int16_t y = 36; y <= 42; y++) {
  const struct cfb_position start = { .x = 2, .y = y };
  const struct cfb_position end = { .x = 1 + filled, .y = y };
  cfb_draw_line(oled, &start, &end);
 }
}

int main(void)
{
 int err=bt_enable(NULL);
 if (err) { LOG_ERR("Bluetooth init failed: %d",err); return err; }
 bool display_ok=device_is_ready(oled) && cfb_framebuffer_init(oled)==0;
 if (display_ok) {
  show_boot_logo();
  display_blanking_off(oled);
 }
 /* Keep the high-impedance divider enabled; the enable pin can only sink. */
 bool battery_ok = gpio_is_ready_dt(&battery_enable) &&
                   gpio_pin_configure_dt(&battery_enable, GPIO_OUTPUT_ACTIVE) == 0 &&
                   adc_is_ready_dt(&battery_adc) && adc_channel_setup_dt(&battery_adc) == 0;
 bool started = false;
 uint16_t seq = 0;
 uint32_t sample_seq = 0;
 uint32_t red = 0, ir = 0;
 int max_err = -ENODEV;
 int64_t next_report = k_uptime_get();
 const int64_t display_start = next_report;
 struct watch_daily_step_record saved_daily = {0};
 int storage_err = load_watch_settings(&saved_daily);
 if (storage_err) LOG_WRN("Watch storage unavailable: %d", storage_err);
 struct watch_screen_order saved_screen_order = screen_order;
 int64_t screen_save_retry = next_report;
 k_work_reschedule(&adv_work,K_NO_WAIT);
 struct watch_daily_steps daily = {.record = saved_daily};
 int64_t next_daily_save = next_report + 60000;
 int64_t daily_save_retry = next_report;
 int64_t next_retry = next_report;
 int64_t next_imu = next_report, motion_until = 0, pulse_motion_until = 0;
 struct watch_steps steps = {0};
 int32_t axes[6] = {0};
 int imu_err = -ENODEV;
 int16_t temperature = 0;
 bool temperature_ok = false;

 while (1) {
  int64_t now = k_uptime_get();
  if (now >= next_imu) {
   next_imu = now + 40;
   imu_err = sample_imu(axes, &temperature, &temperature_ok);
   if (imu_err == 0) {
    watch_steps_sample(&steps, (uint32_t)now, axes);
    if (watch_is_moving(&steps, axes + 3)) {
     motion_until = now + 600;
     pulse_motion_until = now + 120;
    }
   } else {
    watch_steps_break(&steps);
   }
  }
  bool moving = now < motion_until;
  unsigned cadence = 0;
  if (steps.streak >= 3 && (uint32_t)now - steps.last_peak_ms <= 1150) {
   cadence = (steps.previous_interval_ms * PULSE_RATE_HZ + 500) / 1000;
  }
  if (device_is_ready(bus)) {
   if (!started && now >= next_retry) {
    max_err = max30102_start(bus);
    started = max_err == 0;
    next_retry = now + 1000;
    if (started) {
     ++sample_seq; /* Mark a discontinuity after a sensor reset. */
     max_err = -EAGAIN;
    }
   } else if (started) {
    int read_err = max30102_read(&red, &ir, &sample_seq, now < pulse_motion_until,
                                 moving, cadence);
    if (read_err != -EAGAIN) {
     if ((max_err == 0) != (read_err == 0)) next_report = now;
     max_err = read_err;
    }
    if (read_err != 0 && read_err != -EAGAIN) {
     pulse = (struct pulse_state){0};
     oximeter_reset(&oxygen);
     started = false;
     next_retry = now + 1000;
    }
   }
  }
  if (now < next_report) {
   k_sleep(K_MSEC(10));
   continue;
  }
  next_report = now + 1000;
  uint8_t status = 8 | (display_ok ? 4 : 0) | (started ? 1 : 0);
  uint8_t next[12];
  uint8_t raw[sizeof(raw_packet)];
  /* Legacy packet flag bit 1 indicates a reported BPM, which may be held. */
  bool pulse_held = pulse.bpm && pulse.samples_since_estimate >= PULSE_HELD_AFTER_SAMPLES;
  if (pulse.bpm) status |= 2;
  if (pulse_held) status |= 16;
  watch_encode(next,status,seq++,k_uptime_get()/1000,pulse.bpm,0);
  unsigned fresh_lag = pulse.samples_since_estimate < PULSE_RATE_HZ ? pulse.confirmed_lag : 0;
  uint8_t oxygen_trial = oximeter_trial(&oxygen, fresh_lag);
  next[11] = oxygen_trial; /* Uncalibrated trial result. Byte 10 remains the measured field. */
  watch_encode_raw(raw, max_err == 0, red, ir);
  k_mutex_lock(&packet_lock,K_FOREVER);
  memcpy(packet,next,sizeof(packet));
  memcpy(raw_packet, raw, sizeof(raw_packet));
  k_mutex_unlock(&packet_lock);
  if (atomic_get(&linked)) {
   err=bt_gatt_notify(NULL,&watch_svc.attrs[2],next,sizeof(next));
   if (err && err!=-ENOTCONN && err!=-EINVAL) LOG_WRN("Notify: %d",err);
   err = bt_gatt_notify_uuid(NULL, &raw_uuid.uuid, watch_svc.attrs, raw, sizeof(raw));
   if (err && err != -ENOTCONN && err != -EINVAL) LOG_WRN("Raw notify: %d", err);
  }
  uint8_t motion[13] = {0};
  if (imu_err == 0) {
   motion[0] = 1;
   for (unsigned axis = 0; axis < ARRAY_SIZE(axes); ++axis) {
    sys_put_le16((uint16_t)CLAMP(axes[axis], INT16_MIN, INT16_MAX), &motion[1 + 2 * axis]);
   }
  }
  uint8_t activity[sizeof(activity_packet)];
  uint8_t activity_flags = (imu_err == 0 ? 1 : 0) | (moving ? 4 : 0) |
                           (max_err == 0 && ir >= 1000 && red < 1000 ? 8 : 0) |
                           (temperature_ok ? 16 : 0);
  k_mutex_lock(&packet_lock, K_FOREVER);
  int64_t snapshot_ms = k_uptime_get();
  uint32_t epoch = watch_clock_seconds(&watch_time, snapshot_ms);
  uint16_t aqi_value;
  bool aqi_available = watch_aqi_current(&air_quality, snapshot_ms, &aqi_value);
  uint16_t aqi_age = aqi_available ? watch_aqi_age_minutes(&air_quality, snapshot_ms) : 0;
  struct watch_screen_order active_screens = screen_order;
  int64_t order_set_at = screen_order_set_at;
  int16_t offset = watch_time.offset_minutes;
  if (epoch) activity_flags |= 2;
  watch_encode_activity(activity, activity_flags, steps.count, epoch, offset, temperature);
  memcpy(activity_packet, activity, sizeof(activity_packet));
  memcpy(motion_packet, motion, sizeof(motion));
  k_mutex_unlock(&packet_lock);
  if (!storage_err && now >= screen_save_retry &&
      memcmp(&active_screens, &saved_screen_order, sizeof(active_screens)) != 0) {
   uint8_t data[WATCH_SCREEN_PACKET_MAX];
   size_t size = watch_screen_order_encode(&active_screens, data);
   ssize_t written = nvs_write(&watch_storage, SCREEN_ORDER_RECORD_ID, data, size);
   if (written >= 0) {
    saved_screen_order = active_screens;
   } else {
    LOG_WRN("Could not save screen order: %d", (int)written);
    screen_save_retry = now + 60000;
   }
  }
  watch_daily_steps_update(&daily, epoch, offset, steps.count);
  if (!storage_err && epoch && now >= daily_save_retry) {
   bool day_changed = daily.record.day != saved_daily.day;
   uint32_t unsaved_steps = day_changed ? 0 : daily.record.steps - saved_daily.steps;
   if (day_changed || unsaved_steps >= 16 || (unsaved_steps && now >= next_daily_save)) {
    ssize_t written = nvs_write(&watch_storage, DAILY_STEPS_RECORD_ID, &daily.record,
                                sizeof(daily.record));
    next_daily_save = now + 60000;
    if (written >= 0) {
     saved_daily = daily.record;
    } else {
     LOG_WRN("Could not save daily steps: %d", (int)written);
     daily_save_retry = next_daily_save;
    }
   }
  }
  if (atomic_get(&linked)) {
   err = bt_gatt_notify(NULL, &watch_svc.attrs[5], motion, sizeof(motion));
   if (err && err != -ENOTCONN && err != -EINVAL) LOG_WRN("Motion notify: %d", err);
   err = bt_gatt_notify_uuid(NULL, &activity_uuid.uuid, watch_svc.attrs, activity, sizeof(activity));
   if (err && err != -ENOTCONN && err != -EINVAL) LOG_WRN("Activity notify: %d", err);
  }

  if (imu_err == 0) {
   printk("WATCH_TEST MPU Z=%d mm/s^2 | ", axes[2]);
  } else {
   printk("WATCH_TEST MPU err=%d | ", imu_err);
  }
  if (max_err == 0) {
   printk("MAX red=%u IR=%u | BPM=%u | OLED init=%s\n", red, ir, (unsigned int)pulse.bpm, display_ok ? "ok" : "failed");
  } else {
   printk("MAX err=%d | OLED init=%s\n", max_err, display_ok ? "ok" : "failed");
  }

  uint64_t changed_elapsed = order_set_at > display_start ? order_set_at - display_start : 0;
  const enum watch_display_page page = watch_configured_page_at(snapshot_ms - display_start,
                                                              changed_elapsed, &active_screens);
  if (display_ok && page != WATCH_DISPLAY_SPLASH) {
   char line[32];
   char battery_label[5];
   const char *title = NULL;
   bool usb_power = nrf_power_usbregstatus_vbusdet_get(NRF_POWER);
   int battery_percent = watch_battery_label(battery_label, usb_power,
                           battery_ok && !usb_power ? read_battery_mv() : -ENODEV);
   cfb_framebuffer_clear(oled,false);
   switch (page) {
   case WATCH_DISPLAY_TIME:
    title = "Time";
    if (epoch) {
     uint32_t local = ((int64_t)epoch + (int32_t)offset * 60) % 86400;
     snprintf(line, sizeof(line), "%02u:%02u", local / 3600, local / 60 % 60);
    } else {
     snprintf(line, sizeof(line), "Set time");
    }
    break;
   case WATCH_DISPLAY_BPM:
    title = "BPM";
    if (pulse.bpm) {
     snprintf(line, sizeof(line), "%u", (unsigned int)pulse.bpm);
    } else if (max_err == 0) {
     snprintf(line, sizeof(line), "%s", ir >= 1000 ? "Acquiring" : "Check fit");
    } else if (max_err == -EAGAIN) {
     snprintf(line, sizeof(line), "MAX waiting");
    } else {
     snprintf(line, sizeof(line), "MAX err:%d", max_err);
    }
    break;
   case WATCH_DISPLAY_STEPS:
    title = "Steps";
    if (imu_err != 0) {
     snprintf(line, sizeof(line), "MPU err:%d", imu_err);
    } else if (!epoch) {
     snprintf(line, sizeof(line), "Sync time");
    } else {
     snprintf(line, sizeof(line), "%u", daily.record.steps);
    }
    break;
   case WATCH_DISPLAY_AQI:
    title = "AQI";
    if (aqi_available) {
     snprintf(line, sizeof(line), "%u", (unsigned int)aqi_value);
    } else {
     snprintf(line, sizeof(line), "Sync in app");
    }
    break;
   default:
    break;
   }
   if (title) {
    cfb_print(oled,title,0,0);
    int16_t value_y = page == WATCH_DISPLAY_STEPS || page == WATCH_DISPLAY_AQI ? 16 : 24;
    cfb_print(oled,line,0,value_y);
   }
   snprintf(line, sizeof(line), "%4s", battery_label);
   draw_battery(battery_percent);
   cfb_print(oled,line,88,0);
   if (page == WATCH_DISPLAY_BPM) {
    if (oxygen_trial) {
     snprintf(line, sizeof(line), "SpO2~%u%%", (unsigned int)oxygen_trial);
    } else {
     snprintf(line, sizeof(line), "SpO2: --");
    }
    cfb_print(oled,line,0,48);
   }
   if (page == WATCH_DISPLAY_STEPS && epoch && imu_err == 0) {
    draw_step_goal(daily.record.steps);
    cfb_print(oled,"Goal 10k",0,48);
   }
   if (page == WATCH_DISPLAY_AQI && aqi_available) {
    cfb_print(oled,watch_aqi_category(aqi_value),0,32);
    snprintf(line, sizeof(line), "IITD %um", (unsigned int)aqi_age);
    cfb_print(oled,line,0,48);
   }
   if (atomic_get(&linked)) cfb_print(oled,"BLE",90,48);
   cfb_framebuffer_finalize(oled);
  }
  k_sleep(K_MSEC(10));
 }
 return 0;
}

