/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include "app_adc.h" /* <-- önceki modülün başlığı */

#define TEST

#ifdef PROD

#include <zephyr/fs/fs.h>
#include <ff.h>
#include <string.h>
LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define SD_LABEL "SD0"

/* Mount noktası / sürücü */
static FATFS fatfs;
static struct fs_mount_t sd_mnt = {
    .type = FS_FATFS,
    .mnt_point = "/SDHC:",
    .fs_data = &fatfs,
    .storage_dev = (void *)SD_LABEL,
    .flags = FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

/* Writer thread ayarları */
#define WRITER_STACK_SIZE 4096
#define WRITER_PRIORITY -1 /* normal öncelik; istersen negatif (cooperative) yapabilirsin */
K_THREAD_STACK_DEFINE(writer_stack, WRITER_STACK_SIZE);
static struct k_thread writer_thread_data;

/* SD’ye yazma tamponu: büyük ve 512-byte hizalı */
#define OUTBUF_SIZE (64 * 1024) /* 16 KB */
static uint8_t outbuf[OUTBUF_SIZE] __aligned(512);
static size_t out_fill = 0;

/* Senkronizasyon/istatistik */
static struct fs_file_t g_file;
static atomic_t dropped_blocks = ATOMIC_INIT(0);
static size_t bytes_written_total = 0;

/* ---------- SD init & file ---------- */
static int sd_mount(void)
{
    int rc = fs_mount(&sd_mnt);
    if (rc < 0)
    {
        LOG_ERR("fs_mount failed: %d", rc);
        return rc;
    }
    LOG_INF("SD mounted at %s", sd_mnt.mnt_point);
    return 0;
}

static int sd_open_file(const char *path)
{
    fs_file_t_init(&g_file);
    int rc = fs_open(&g_file, path, FS_O_CREATE | FS_O_WRITE);
    if (rc < 0)
    {
        LOG_ERR("fs_open('%s') failed: %d", path, rc);
        return rc;
    }
    out_fill = 0;
    bytes_written_total = 0;
    return 0;
}

static int sd_flush_buf(void)
{
    if (out_fill == 0)
        return 0;

    ssize_t wr = fs_write(&g_file, outbuf, out_fill);
    if (wr < 0)
    {
        LOG_ERR("fs_write failed: %d", (int)wr);
        return (int)wr;
    }
    bytes_written_total += (size_t)wr;
    out_fill = 0;
    return 0;
}

/* Periyodik sync: veri kaybını azaltır; çok sık yapma (performans!) */
static int sd_maybe_sync(size_t every_bytes)
{
    if (bytes_written_total >= every_bytes)
    {
        int rc = fs_sync(&g_file);
        if (rc < 0)
        {
            LOG_ERR("fs_sync failed: %d", rc);
            return rc;
        }
        bytes_written_total = 0;
    }
    return 0;
}

/* ---------- Writer thread ---------- */
static void writer_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    /* 1) SD mount + dosya aç */
    if (sd_mount() < 0)
    {
        LOG_ERR("SD mount failed, writer exiting");
        return;
    }
    if (sd_open_file("/SDHC:/adc_500ksps.bin") < 0)
    {
        LOG_ERR("open file failed, writer exiting");
        return;
    }
    LOG_INF("Writing ADC stream to /SDHC:/adc_500ksps.bin ...");

    /* 2) ADC başlat */
    if (adc_stream_init() != 0)
    {
        LOG_ERR("adc_stream_init failed");
        return;
    }
    (void)adc_stream_start();

    /* 3) Blokları tüket ve yaz */
    struct adc_stream_block blk;
    const size_t SYNC_EVERY = 256 * 1024; /* ~256 KB’te bir sync */

    while (1)
    {
        int rc = adc_stream_get(&blk, K_FOREVER);
        if (rc != 0)
        {
            continue;
        }

        /* Gelen yarım blok byte sayısı */
        size_t bytes = blk.count * sizeof(uint16_t);
        const uint8_t *src = (const uint8_t *)blk.ptr;

        /* Büyük blokları yazarken mümkün olduğunca outbuf’u doldurup yazalım */
        size_t remaining = bytes;
        while (remaining)
        {
            size_t space = OUTBUF_SIZE - out_fill;
            if (space == 0)
            {
                /* Tampon dolu → diske yaz */
                if (sd_flush_buf() < 0)
                {
                    /* Hata durumunda istersen kır, ya da loglayıp devam et */
                    LOG_ERR("flush error; dropping incoming data");
                    break;
                }
                space = OUTBUF_SIZE;
            }
            size_t chunk = (remaining < space) ? remaining : space;
            memcpy(&outbuf[out_fill], src + (bytes - remaining), chunk);
            out_fill += chunk;
            remaining -= chunk;
        }

        /* Tampon iyice dolduysa hemen flush et (opsiyonel; zaten while üstte flush ediyor) */
        if (out_fill == OUTBUF_SIZE)
        {
            (void)sd_flush_buf();
        }

        /* Periyodik sync */
        (void)sd_maybe_sync(SYNC_EVERY);
    }

    /* normalde buraya gelmeyiz; bir durdurma mekanizması eklersen flush+close yap */
    (void)sd_flush_buf();
    (void)fs_sync(&g_file);
    (void)fs_close(&g_file);
}

/* ---------- main ---------- */
int main(void)
{
    /* Writer thread’i başlat (ADC başlatmayı da içinde yapıyor) */
    k_thread_create(&writer_thread_data, writer_stack, K_THREAD_STACK_SIZEOF(writer_stack),
                    writer_thread, NULL, NULL, NULL, WRITER_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&writer_thread_data, "sd_writer");

    return 0;
}
#endif

#if defined(TEST)

LOG_MODULE_REGISTER(app_test, LOG_LEVEL_INF);

static void consume_some_blocks(int seconds)
{
    uint64_t end = k_uptime_get() + (seconds * 1000);
    struct adc_stream_block b;
    uint32_t cnt = 0;

    while ((int64_t)(end - k_uptime_get()) > 0)
    {
        if (adc_stream_get(&b, K_MSEC(500)) == 0)
        {
            /* simple stats: min/max/avg of first 8 samples */
            uint16_t minv = 0xFFFF, maxv = 0, sum = 0;
            size_t n = b.count > 8 ? 8 : b.count;
            for (size_t i = 0; i < n; ++i)
            {
                uint16_t v = b.ptr[i];
                minv = (v < minv) ? v : minv;
                maxv = (v > maxv) ? v : maxv;
                sum += v;
            }
            LOG_INF("#%u buf=%u half=%u first8[min=%u max=%u avg=%u]",
                    b.seq, b.buf_id, b.second_half, minv, maxv, (unsigned)(sum / (n ? n : 1)));
            cnt++;
        }
        else
        {
            LOG_WRN("no blocks yet...");
        }
    }
    LOG_INF("Consumed %u blocks", cnt);
}

int main(void)
{
    int rc = adc_stream_init();
    if (rc)
    {
        LOG_ERR("adc_stream_init=%d", rc);
        return rc;
    }
    adc_stream_start();
    LOG_INF("ADC test started: %u Hz, %u-bit",
            (unsigned)adc_stream_sample_rate_hz(),
            (unsigned)adc_stream_resolution_bits());

    consume_some_blocks(10);

    adc_stream_stop();
    LOG_INF("Done.");
    return 0;
}
#

#endif