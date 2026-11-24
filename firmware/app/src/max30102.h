/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WATCH_MAX30102_H
#define WATCH_MAX30102_H
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <errno.h>

#define MAX30102_ADDR 0x57
#define MAX30102_FIFO_WR 0x04
#define MAX30102_FIFO_DATA 0x07
#define MAX30102_MODE 0x09
#define MAX30102_PART_ID 0xff

static inline int max30102_start(const struct device *bus)
{
 uint8_t id;
 int err = i2c_reg_read_byte(bus, MAX30102_ADDR, MAX30102_PART_ID, &id);
 if (err) return err;
 if (id != 0x15) return -ENODEV;

 err = i2c_reg_write_byte(bus, MAX30102_ADDR, MAX30102_MODE, 0x40);
 if (err) return err;
 uint8_t mode;
 for (int attempt = 0; attempt < 10; attempt++) {
  k_sleep(K_MSEC(5));
  err = i2c_reg_read_byte(bus, MAX30102_ADDR, MAX30102_MODE, &mode);
  if (err) return err;
  if (!(mode & 0x40)) break;
 }
 if (mode & 0x40) return -ETIMEDOUT;

 /* Clear FIFO pointers, disable averaging, use 50 Hz at 18 bits,
  * and drive each LED at about 6.4 mA for the bench test. */
 const uint8_t settings[][2] = {
  {0x04, 0}, {0x05, 0}, {0x06, 0}, {0x08, 0x00}, {0x0a, 0x23},
  {0x0c, 0x20}, {0x0d, 0x20}, {MAX30102_MODE, 0x03},
 };
 for (size_t i = 0; i < ARRAY_SIZE(settings); i++) {
  err = i2c_reg_write_byte(bus, MAX30102_ADDR, settings[i][0], settings[i][1]);
  if (err) return err;
 }
 return 0;
}

struct max30102_sample {
 uint32_t red;
 uint32_t ir;
};

static inline int max30102_read_fifo(const struct device *bus,
                                    struct max30102_sample *samples, unsigned capacity)
{
 uint8_t pointers[3];
 int err = i2c_burst_read(bus, MAX30102_ADDR, MAX30102_FIFO_WR, pointers, sizeof(pointers));
 if (err) return err;
 if (pointers[1]) return -EOVERFLOW;
 unsigned count = (pointers[0] - pointers[2]) & 0x1f;
 if (!count) return -EAGAIN;
 if (count > capacity) count = capacity;
 for (unsigned i = 0; i < count; ++i) {
  uint8_t data[6];
  err = i2c_burst_read(bus, MAX30102_ADDR, MAX30102_FIFO_DATA, data, sizeof(data));
  if (err) return err;
  samples[i].red = ((uint32_t)(data[0] & 3) << 16) | ((uint32_t)data[1] << 8) | data[2];
  samples[i].ir = ((uint32_t)(data[3] & 3) << 16) | ((uint32_t)data[4] << 8) | data[5];
 }
 return (int)count;
}
#endif
