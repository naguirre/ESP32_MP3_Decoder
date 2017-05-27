#ifndef __VS1053_H__
#define __VS1053_H__

#include <stdint.h>

uint8_t vs1053_init(void);
void vs1053_volume_set(uint8_t left, uint8_t right);
void vs1053_reset(void);
void vs1053_software_reset(void);
void vs1053_sine_test(uint8_t val, uint32_t duration);
void vs1053_write_data(void *data, uint8_t len);
int vs1053_write_music(uint8_t *data, uint32_t len);

#endif /* __VS1053_H__ */
