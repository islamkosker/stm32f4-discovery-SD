#pragma once 




#include <zephyr/kernel.h>
#include <stddef.h>
#include <stdint.h>

/* Bir örnek bloğunu tanımlar: kopyasız göstergeler */
struct adc_stream_block {
    const uint16_t *ptr;   /* örneklerin başlangıcı */
    size_t          count; /* örnek adedi (uint16_t) */
    uint32_t        seq;   /* artan sıra numarası */
    bool            second_half; /* true => buffer yarısının 2. kısmı */
    uint8_t         buf_id; /* 0: M0, 1: M1 */
};

/* Başlat/Çalıştır/Durdur ve blok alma API'si */
int  adc_stream_init(void);
int  adc_stream_start(void);
void adc_stream_stop(void);

/* Blok tüketimi (SPI thread’in çağıracağı): timeout ile bekler */
int  adc_stream_get(struct adc_stream_block *out, k_timeout_t to);

/* Hedef örnekleme parametreleri (bilgi amaçlı) */
uint32_t adc_stream_sample_rate_hz(void);
uint8_t  adc_stream_resolution_bits(void);
