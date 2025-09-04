#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include "main.h"

// Number of samples to be made on the sequence for each channel.
#define CONFIG_SEQUENCE_SAMPLES 12
// Set the resolution of the sequence readings.
#define CONFIG_SEQUENCE_RESOLUTION 12

#define ADC_NODE DT_NODELABEL(adc1)
static const struct device *dev_adc = DEVICE_DT_GET(ADC_NODE);

static const struct adc_channel_cfg channel_cfgs[] = {
    DT_FOREACH_CHILD_SEP(ADC_NODE, ADC_CHANNEL_CFG_DT, (, ))};

// Internal referance. Look overlay file
// static uint32_t vrefs_mv[] = {DT_FOREACH_CHILD_SEP(ADC_NODE, CHANNEL_VREF, (, ))};

/* Get the number of channels defined on the DTS. */
#define CHANNEL_COUNT ARRAY_SIZE(channel_cfgs)

static uint16_t channel_reading[CONFIG_SEQUENCE_SAMPLES][CHANNEL_COUNT];

const struct adc_sequence_options options = {
    .extra_samplings = CONFIG_SEQUENCE_SAMPLES - 1,
    .interval_us = 0,
};

/* Configure the sampling sequence to be made. */
struct adc_sequence sequence = {
    .buffer = channel_reading,
    /* buffer size in bytes, not number of samples */
    .buffer_size = sizeof(channel_reading),
    .resolution = CONFIG_SEQUENCE_RESOLUTION,
    .options = &options,
};

static inline void read_adc(void)
{
    int err;
    uint32_t count = 0;
    printf("ADC sequence reading [%u]:\n", count++);

    err = adc_read(dev_adc, &sequence);
    if (err < 0)
    {
        printf("Could not read (%d)\n", err);
    }

    for (size_t channel_index = 0U; channel_index < CHANNEL_COUNT; channel_index++)
    {
        int32_t val;

        printf("- %s, channel %" PRId32 ", %" PRId32 " sequence samples:\n",
               dev_adc->name, channel_cfgs[channel_index].channel_id,
               CONFIG_SEQUENCE_SAMPLES);

        for (size_t sample_index = 0U; sample_index < CONFIG_SEQUENCE_SAMPLES; sample_index++)
        {

            val = channel_reading[sample_index][channel_index];

            printf("- - %" PRId32, val);
        }
    }
}

uint8_t init_adc(void)
{

    int err;
    for (size_t i = 0U; i < CHANNEL_COUNT; i++)
    {
        sequence.channels |= BIT(channel_cfgs[i].channel_id);
        err = adc_channel_setup(dev_adc, &channel_cfgs[i]);
        if (err < 0)
        {
            printf("Could not setup channel #%d (%d)\n", i, err);
            return 0;
        }
    }
    printk("Adc is ready!\n");
    read_adc();

    return err;
}