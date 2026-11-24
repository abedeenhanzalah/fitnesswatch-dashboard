#include <assert.h>
#include <string.h>
#include "telemetry.h"

int main(void)
{
 uint8_t packet[WATCH_RAW_PACKET_SIZE];
 const uint8_t sample[] = {1, 1, 0x45, 0x23, 0x01, 0, 0xff, 0xff, 0x03, 0};
 const uint8_t zero_sample[] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
 const uint8_t unavailable[] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0};

 watch_encode_raw(packet, true, 0x12345, 0x3ffff);
 assert(memcmp(packet, sample, sizeof(packet)) == 0);
 watch_encode_raw(packet, true, 0, 0);
 assert(memcmp(packet, zero_sample, sizeof(packet)) == 0);
 watch_encode_raw(packet, false, 0x12345, 0x3ffff);
 assert(memcmp(packet, unavailable, sizeof(packet)) == 0);
}
