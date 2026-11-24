/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/crc.h>
#include <stdarg.h>
#include <stdio.h>
#include "max30102.h"

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(xiao_i2c));
static const struct device *const imu = DEVICE_DT_GET(DT_NODELABEL(mpu6050));
static const struct device *const oled = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const usb = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void emit(const char *format, ...)
{
 char frame[112];
 va_list args;
 va_start(args, format);
 int length = vsnprintf(frame, sizeof(frame) - 7, format, args);
 va_end(args);
 if (length < 0 || (size_t)length >= sizeof(frame) - 7) return;

 uint16_t crc = crc16_itu_t(0xffff, (const uint8_t *)frame, length);
 length += snprintf(frame + length, sizeof(frame) - length, "*%04x\n", (unsigned int)crc);
 for (int i = 0; i < length; ++i) uart_poll_out(usb, frame[i]);
}

static void record_motion(uint32_t sequence, uint32_t now)
{
 struct sensor_value axes[6];
 int err = device_is_ready(imu) ? sensor_sample_fetch(imu) : -ENODEV;
 if (!err) err = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, axes);
 if (!err) err = sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, axes + 3);
 if (err) {
  emit("E,%u,IMU,%d", now, err);
  return;
 }
 int32_t scaled[6];
 for (unsigned i = 0; i < ARRAY_SIZE(scaled); ++i) {
  scaled[i] = sensor_value_to_micro(&axes[i]) / 1000;
 }
 emit("M,%u,%u,%d,%d,%d,%d,%d,%d", sequence, now,
      scaled[0], scaled[1], scaled[2], scaled[3], scaled[4], scaled[5]);
}

int main(void)
{
 if (device_is_ready(oled) && cfb_framebuffer_init(oled) == 0) {
  cfb_framebuffer_invert(oled);
  display_blanking_off(oled);
  cfb_framebuffer_clear(oled, false);
  cfb_print(oled, "USB calibration", 0, 0);
  cfb_print(oled, "Run capture", 0, 16);
  cfb_framebuffer_finalize(oled);
 }
 if (!device_is_ready(usb)) return -ENODEV;

 bool recording = false;
 bool optical_started = false;
 uint32_t optical_sequence = 0, motion_sequence = 0;
 int64_t next_retry = 0, next_motion = 0;
 while (1) {
  uint32_t dtr = 0;
  if (uart_line_ctrl_get(usb, UART_LINE_CTRL_DTR, &dtr) || !dtr) {
   recording = false;
   k_sleep(K_MSEC(25));
   continue;
  }
  int64_t now = k_uptime_get();
  unsigned char command;
  while (uart_poll_in(usb, &command) == 0) {
   if (command == 'S' && !recording) {
    recording = true;
    optical_started = false;
    next_retry = now;
    next_motion = now;
    ++optical_sequence;
   } else if (command == 'X') {
    recording = false;
   }
  }
  if (!recording) {
   k_sleep(K_MSEC(10));
   continue;
  }
  if (!optical_started && now >= next_retry) {
   int err = device_is_ready(bus) ? max30102_start(bus) : -ENODEV;
   optical_started = err == 0;
   next_retry = now + 1000;
   if (err) emit("E,%u,PPG,%d", (uint32_t)now, err);
  }
  if (optical_started) {
   struct max30102_sample samples[31];
   int count = max30102_read_fifo(bus, samples, ARRAY_SIZE(samples));
   if (count < 0 && count != -EAGAIN) {
    emit("E,%u,PPG,%d", (uint32_t)now, count);
    ++optical_sequence;
    optical_started = false;
    next_retry = now + 1000;
   }
   for (int i = 0; i < count; ++i) {
    emit("P,%u,%u,%u,%u", optical_sequence++, (uint32_t)now,
         samples[i].red, samples[i].ir);
   }
  }
  if (now >= next_motion) {
   record_motion(motion_sequence++, (uint32_t)now);
   next_motion += 40;
   if (next_motion <= now) next_motion = now + 40;
  }
  k_sleep(K_MSEC(5));
 }
}
