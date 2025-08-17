# STM32F4 (Zephyr) – 500 kS/s ADC → SD (SPI) Streamer

This project samples **ADC1/IN10 (PC0)** at **500 kS/s (2 µs period)** using **TIM2 TRGO** as the external trigger and moves data via **DMA (double-buffer)** into RAM. A writer thread batches raw samples into a 512‑byte–aligned buffer and writes them to a **FAT** volume on an **SPI SD card**.

> **Output file:** `/SD:/adc_500ksps.bin` (by default). Samples are **little‑endian 16‑bit unsigned**.

---

## DeviceTree (overlay) – example

Adapt the following to your board. The important bits are `zephyr,sdhc-spi-slot` and the **disk-name** matching the **mount point** you use in code:

```dts
&spi1 {
    status = "okay";
    pinctrl-0 = <&spi1_sck_pa5 &spi1_miso_pa6 &spi1_mosi_pa7>;
    pinctrl-names = "default";
    cs-gpios = <&gpioe 11 GPIO_ACTIVE_LOW>;
    spi-max-frequency = <24000000>;   /* 24 MHz for decent throughput */

    sdhc0: sdhc@0 {
        compatible = "zephyr,sdhc-spi-slot";
        reg = <0>;
        status = "okay";

        mmc: mmc {
            compatible = "zephyr,sdmmc-disk";
            disk-name = "SD";         /* <-- then mount at "/SD:" */
            status = "okay";
        };
    };
};

&adc1 { status = "okay"; };

/* Inform the app which ADC channel we use */
/ {
    zephyr,user {
        io-channels = <&adc1 10>;    /* ADC1 channel 10 (PC0) */
    };
};
```

---

## Kconfig (prj.conf)

Minimum set (add to what you already have):

```ini
# ADC + DMA
CONFIG_ADC=y
CONFIG_ADC_STM32=y
CONFIG_DMA=y
CONFIG_DMA_STM32=y

# SPI + SD over SPI + FAT
CONFIG_SPI=y
CONFIG_DISK_ACCESS=y
CONFIG_DISK_DRIVER_SDMMC=y    # SD/MMC disk driver
CONFIG_SDMMC_SPI=y            # SD driver over SPI
CONFIG_FILE_SYSTEM=y
CONFIG_FAT_FILESYSTEM_ELM=y
CONFIG_FS_FATFS_LFN=y         # long filenames (optional)

# Stacks, logging
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_ISR_STACK_SIZE=2048
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2    # drop to 1 or 0 for max throughput
```

> If your Zephyr version uses slightly different symbols for SD‑over‑SPI, enable the SD/MMC disk driver and its SPI mode accordingly.

---

## Build & Flash

```bash
west build -b stm32f4_disco . -t pristine
west flash
```

- Ensure toolchain/SDK is installed and your board is connected.
- Format the SD card as **FAT32** (single partition).

---

## File format & rate

- **File:** `adc_500ksps.bin` on the SD card root.
- **Sample format:** `uint16_t` little‑endian; nominal range 0–4095 at 12‑bit resolution.
- **Rate:** 500 000 samples/second ⇒ **~1.0 MB/s** sustained to SD. Use fast cards and keep SPI at ≥ 18–24 MHz.

---

## Throughput tips

- Use **fast SD** cards and set `spi-max-frequency` to a safe high value (e.g., 24 MHz).
- Keep the writer thread at **high priority** (e.g., `-1` or `-2` cooperative) so it empties the queue quickly.
- Increase the message queue depth (e.g., **128** blocks) for >60 ms buffering headroom.
- Use a **large, 512‑aligned** output buffer (e.g., **64 KB**) and flush only when full.
- Call `fs_sync()` **sparingly** (e.g., every few megabytes) to reduce latency.

---

## Verifying the 2 µs cadence

- Probe **TIM2 TRGO** or toggle a spare GPIO inside the DMA HT/TC ISR to see **~0.512 ms** half‑buffer periods (256 samples × 2 µs).
- Optionally dump a few seconds of data and measure zero‑crossing intervals offline.

---

## Known caveats

- If mounting fails, make sure **`disk-name` ↔ mount point ↔ `storage_dev`** all match (e.g., `disk-name="SD"` ⇔ mount at `"/SD:"` ⇔ `storage_dev="SD"`).
- For very high‑impedance sources, increase ADC **sample time** or buffer the signal.
- If you see dropped blocks, raise SD SPI clock, enlarge buffers, and increase queue depth.

---

## Roadmap ideas

- Add a header to the binary with sample rate, resolution, and timestamp.
- Replace FAT with raw block logging for maximal throughput.
- Add a graceful stop command that flushes and closes the file.



## 📝 Environment

- `$env:ZEPHYR_BASE="C:\Users\Admin\Developer\sdk\zephyr\zephyrproject\zephyr"`
- `$env:ZEPHYR_SDK_INSTALL_DIR="C:\Users\Admin\Developer\sdk\zephyr\zephyr-sdk-0.17.2"`
- `C:\Users\Admin\Developer\sdk\zephyr\zephyrproject\.venv\Scripts\Activate.ps1`
- `C:\Windows\system32\cmd.exe /d /s /c "west build -p auto -b stm32f4_disco  . --build-dir .build  -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DEXTRA_DTC_OVERLAY_FILE=stm32f4_disco_stm32f407x.overlay"`
- `C:\Windows\system32\cmd.exe /d /s /c "west build -p auto -b nucleo_f070rb  . --build-dir .build  -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DEXTRA_DTC_OVERLAY_FILE=nucleo_f070rb.overlay"`
- `STM32_Programmer_CLI.exe -c port=SWD freq=4000 -w .build\zephyr\zephyr.bin 0x08000000 -v -rst`
- `plink -serial COM8 -sercfg 115200,8,n,1,N  `
