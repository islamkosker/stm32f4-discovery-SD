#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <string.h>
#include "app_adc.h"

LOG_MODULE_REGISTER(adc_f0test, LOG_LEVEL_INF);

/* ---------- ADC channel from DT ----------
 * Use: / { zephyr,user { io-channels = <&adc 0>; }; };
 * On Nucleo-F070RB, ADC_IN0 is PA0 (Arduino A0). */
#define USER_NODE DT_PATH(zephyr_user)
BUILD_ASSERT(DT_NODE_HAS_PROP(USER_NODE, io_channels), "zephyr,user: io-channels missing");
static const struct adc_dt_spec adc_ch = ADC_DT_SPEC_GET_BY_IDX(USER_NODE, 0);

/* Test sampler parameters */
#define BUF_SAMPLES     512U
#define HALF_SAMPLES    (BUF_SAMPLES/2)
#define MSGQ_DEPTH      256  /* generous buffer for writer stalls */
#define ADC_RES_BITS    12
#define TEST_RATE_HZ    20000 /* ~20 kS/s in polling (test only) */

static uint16_t buf_m0[BUF_SAMPLES] __aligned(4);
static uint16_t buf_m1[BUF_SAMPLES] __aligned(4);
K_MSGQ_DEFINE(adc_block_q, sizeof(struct adc_stream_block), MSGQ_DEPTH, 4);

static struct k_thread sampler_thread;
#define SAMPLER_STACK 2048
K_THREAD_STACK_DEFINE(sampler_stack, SAMPLER_STACK);

static atomic_t running = ATOMIC_INIT(0);
static uint32_t g_seq;

/* helpers */
static int one_sample_read(uint16_t *out)
{
    struct adc_sequence seq = {
        .channels    = BIT(adc_ch.channel_id),
        .buffer      = out,
        .buffer_size = sizeof(*out),
        .resolution  = ADC_RES_BITS,
        .oversampling = 0,
        .calibrate = false,
    };
    int rc = adc_read(adc_ch.dev, &seq);
    if (rc == 0) {
        /* STM32 driver returns right-aligned sample; mask to 12-bit */
        *out &= 0x0FFF;
    }
    return rc;
}

static void sampler_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    uint8_t cur = 0; /* 0 => m0, 1 => m1 */
    const int sleep_us = 1000000 / TEST_RATE_HZ; /* coarse pacing */

    while (atomic_get(&running)) {
        uint16_t *base = (cur == 0) ? buf_m0 : buf_m1;

        /* fill first half */
        for (size_t i = 0; i < HALF_SAMPLES; ++i) {
            (void)one_sample_read(&base[i]);
            k_busy_wait(0); /* allow ISR preemption; no delay */
        }
        struct adc_stream_block b0 = {
            .ptr = base,
            .count = HALF_SAMPLES,
            .seq = g_seq++,
            .second_half = false,
            .buf_id = cur,
        };
        (void)k_msgq_put(&adc_block_q, &b0, K_NO_WAIT);

        /* fill second half */
        for (size_t i = 0; i < HALF_SAMPLES; ++i) {
            (void)one_sample_read(&base[HALF_SAMPLES + i]);
            k_busy_wait(0);
        }
        struct adc_stream_block b1 = {
            .ptr = &base[HALF_SAMPLES],
            .count = HALF_SAMPLES,
            .seq = g_seq++,
            .second_half = true,
            .buf_id = cur,
        };
        (void)k_msgq_put(&adc_block_q, &b1, K_NO_WAIT);

        cur ^= 1u;
        /* coarse pacing to avoid burning CPU; remove for max speed */
        k_busy_wait(sleep_us);
    }
}

int adc_stream_init(void)
{
    if (!device_is_ready(adc_ch.dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }
    int rc = adc_channel_setup_dt(&adc_ch);
    if (rc) {
        LOG_ERR("adc_channel_setup_dt: %d", rc);
        return rc;
    }
    g_seq = 0;
    return 0;
}

int adc_stream_start(void)
{
    if (atomic_set(&running, 1) == 1) {
        return 0; /* already running */
    }
    k_tid_t tid = k_thread_create(&sampler_thread, sampler_stack, SAMPLER_STACK,
                                  sampler_fn, NULL, NULL, NULL,
                                  -1 /* high prio cooperative */, 0, K_NO_WAIT);
    k_thread_name_set(tid, "adc_sampler");
    return 0;
}

void adc_stream_stop(void)
{
    if (atomic_set(&running, 0) == 0) return;
    /* allow thread to finish current cycle */
    k_msleep(10);
}

int adc_stream_get(struct adc_stream_block *out, k_timeout_t to)
{
    return k_msgq_get(&adc_block_q, out, to);
}

uint32_t adc_stream_sample_rate_hz(void) { return TEST_RATE_HZ; }
uint8_t  adc_stream_resolution_bits(void) { return ADC_RES_BITS; }
