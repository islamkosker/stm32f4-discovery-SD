#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "app_adc.h"

#include "logger.h"

LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

/* STM32 LL başlıkları */
#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_ll_rcc.h>
#include <stm32f4xx_ll_adc.h>
#include <stm32f4xx_ll_tim.h>
#include <stm32f4xx_ll_dma.h>
#include <stm32f4xx_ll_gpio.h>


/* ================== Settings ================== */
/* 500 kS/s için 2 us periyot */
#define ADC_STREAM_FS_HZ 500000U

/* Örnek zamanı ve ADC clock (empedansa göre 28–56 iyi).
 * ADC clk = PCLK2/4 (~21 MHz varsayımı: 84 MHz PCLK2). */
#define ADC_SAMPLE_TIME LL_ADC_SAMPLINGTIME_28CYCLES
#define ADC_RES_BITS LL_ADC_RESOLUTION_12B

/* Blok boyutu: toplam buffer N, yarı blok N/2.
 * Büyük blok = daha az kesme, ama daha çok gecikme. */
#define BUF_SAMPLES 512U

#define TIM_APB1_TIMER_CLOCK_HZ 84000000UL /* 84 MHz */
#define AUTO_RELOAD (TIM_APB1_TIMER_CLOCK_HZ / ADC_STREAM_FS_HZ) - 1UL

#if defined(ADC) /* STM32F4 çoğunlukla burayı kullanır */
#define ADC_COMMON ADC
#elif defined(ADC123_COMMON) /* bazı serilerde böyle */
#define ADC_COMMON ADC123_COMMON
#elif defined(__LL_ADC_COMMON_INSTANCE)
/* Bazı LL sürümlerinde bu makro mevcut: ortak instansı ADC1'den türetir */
#define ADC_COMMON __LL_ADC_COMMON_INSTANCE(ADC1)
#else
#error "ADC common instance bulunamadı: LL sürümü için ortak ADC sembolünü kontrol et."
#endif

/* =============================================================== */

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#if !DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, io_channels)
#error "zephyr,user/io-channels tanımlı değil!"
#endif

/* Double-buffer RAM */
static uint16_t buf_m0[BUF_SAMPLES];
static uint16_t buf_m1[BUF_SAMPLES];

/* Kopyasız aktarım için mesaj kuyruğu */

K_MSGQ_DEFINE(adc_block_q, sizeof(struct adc_stream_block), 256, 4);

/* Sıra numarası (ISR'da artar) */
static volatile uint32_t g_seq;

/* ---------- Yardımcılar ---------- */
static inline void gpio_init_pc0_analog(void)
{
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
	LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_0, LL_GPIO_MODE_ANALOG);
	LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_0, LL_GPIO_PULL_NO);
}

/* TIM2 TRGO = 500 kHz: APB1 timer clock=84 MHz varsayımıyla ARR = 167, PSC = 0 */
static inline void tim2_init_trgo_500k(void)
{
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

	LL_TIM_DisableCounter(TIM2);
	LL_TIM_SetPrescaler(TIM2, 0);
	LL_TIM_SetAutoReload(TIM2, AUTO_RELOAD); /* 84MHz/500k=168 => ARR=167 */
	LL_TIM_SetCounterMode(TIM2, LL_TIM_COUNTERMODE_UP);
	LL_TIM_SetTriggerOutput(TIM2, LL_TIM_TRGO_UPDATE);
	LL_TIM_EnableCounter(TIM2);
}

static inline void adc_set_exten_rising(void)
{
	/* EXTEN: 00=Disabled, 01=Rising, 10=Falling, 11=Both */
	MODIFY_REG(ADC1->CR2, ADC_CR2_EXTEN, (0x1U << ADC_CR2_EXTEN_Pos));
}

/* ADC1: IN10 (PC0), external trigger TIM2 TRGO, DMA unlimited */
static inline void adc1_init(void)
{
	/* Saat ver */
	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC1);

	/* ADC ortak clock (PCLK2/4 ≈ 21 MHz) */
#if defined(LL_ADC_SetCommonClock)
	LL_ADC_SetCommonClock(ADC_COMMON, LL_ADC_CLOCK_SYNC_PCLK_DIV4);
#endif

	/* ADC'yi temiz durum: LL_ADC_DeInit varsa onu, yoksa RCC reset kullan */
#if defined(LL_ADC_DeInit)
	LL_ADC_DeInit(ADC1);
#else
	SET_BIT(RCC->APB2RSTR, RCC_APB2RSTR_ADCRST);
	CLEAR_BIT(RCC->APB2RSTR, RCC_APB2RSTR_ADCRST);
#endif

	/* EOC kesmelerini kapat (API farklı olabilir) */
#if defined(LL_ADC_DisableIT_EOCS)
	LL_ADC_DisableIT_EOCS(ADC1);
#elif defined(LL_ADC_DisableIT_EOC)
	LL_ADC_DisableIT_EOC(ADC1);
#endif

	/* Tek kanal: IN10 (PC0), örnek zamanı 28 cycles */
	LL_ADC_REG_SetSequencerLength(ADC1, LL_ADC_REG_SEQ_SCAN_DISABLE);
	LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_10);
	LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_10, LL_ADC_SAMPLINGTIME_28CYCLES);

	/* 12-bit çözünürlük */
	LL_ADC_SetResolution(ADC1, LL_ADC_RESOLUTION_12B);

	/* Harici tetik: kaynak = TIM2 TRGO, kenar = rising */
#if defined(LL_ADC_REG_SetTriggerSource)
	LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_EXT_TIM2_TRGO);
#endif
	adc_set_exten_rising();

	/* DMA: sınırsız istek, tek-dönüşüm (TRGO ile başlar) */
	LL_ADC_REG_SetDMATransfer(ADC1, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
	LL_ADC_REG_SetContinuousMode(ADC1, LL_ADC_REG_CONV_SINGLE);

	/* Enable + kısa stabilizasyon bekleyebilirsin */
	LL_ADC_Enable(ADC1);
	for (volatile int i = 0; i < 1000; ++i)
	{
		__NOP();
	}
}

/* DMA2 Stream0 Channel0: ADC1 regular data → buf_m0/m1, circular double-buffer */
static inline void dma2_stream0_init(void)
{
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

	LL_DMA_DeInit(DMA2, LL_DMA_STREAM_0);
	LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_0, LL_DMA_CHANNEL_0);
	LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_0, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
	LL_DMA_SetStreamPriorityLevel(DMA2, LL_DMA_STREAM_0, LL_DMA_PRIORITY_HIGH);
	LL_DMA_SetMode(DMA2, LL_DMA_STREAM_0, LL_DMA_MODE_CIRCULAR);
	LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_0, LL_DMA_PERIPH_NOINCREMENT);
	LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_0, LL_DMA_MEMORY_INCREMENT);
	LL_DMA_SetPeriphSize(DMA2, LL_DMA_STREAM_0, LL_DMA_PDATAALIGN_HALFWORD);
	LL_DMA_SetMemorySize(DMA2, LL_DMA_STREAM_0, LL_DMA_MDATAALIGN_HALFWORD);

	LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_0,
						   LL_ADC_DMA_GetRegAddr(ADC1, LL_ADC_DMA_REG_REGULAR_DATA),
						   (uint32_t)buf_m0,
						   LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

	LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_0, BUF_SAMPLES);

	/* Double buffer: M1 adresi */
	LL_DMA_SetMemory1Address(DMA2, LL_DMA_STREAM_0, (uint32_t)buf_m1);
	LL_DMA_EnableDoubleBufferMode(DMA2, LL_DMA_STREAM_0);

	/* Yarım/tam transfer kesmeleri */
	LL_DMA_EnableIT_HT(DMA2, LL_DMA_STREAM_0);
	LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_0);

	NVIC_SetPriority(DMA2_Stream0_IRQn, 0);
	NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/* ---- Public API ---- */

int adc_stream_init(void)
{
	/* PC0 analog, timer, adc, dma init */
	gpio_init_pc0_analog();
	tim2_init_trgo_500k();
	adc1_init();
	dma2_stream0_init();
	g_seq = 0;
	return 0;
}

int adc_stream_start(void)
{
	/* DMA akışı başlasın, sonra ADC tetiklerine cevap vermeye hazır.
	   Sıra: DMA enable -> ADC start (ext trig) */
	LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_0);
	LL_ADC_REG_StartConversionExtTrig(ADC1, LL_ADC_REG_TRIG_EXT_RISING);
	return 0;
}

void adc_stream_stop(void)
{
	LL_ADC_REG_StopConversionExtTrig(ADC1);
	LL_DMA_DisableIT_HT(DMA2, LL_DMA_STREAM_0);
	LL_DMA_DisableIT_TC(DMA2, LL_DMA_STREAM_0);
	LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_0);
}

int adc_stream_get(struct adc_stream_block *out, k_timeout_t to)
{
	return k_msgq_get(&adc_block_q, out, to);
}

uint32_t adc_stream_sample_rate_hz(void) { return ADC_STREAM_FS_HZ; }
uint8_t adc_stream_resolution_bits(void)
{
	switch (ADC_RES_BITS)
	{
	case LL_ADC_RESOLUTION_12B:
		return 12;
	case LL_ADC_RESOLUTION_10B:
		return 10;
	case LL_ADC_RESOLUTION_8B:
		return 8;
	case LL_ADC_RESOLUTION_6B:
		return 6;
	default:
		return 12;
	}
}

/* ---- DMA ISR ----
 * Double-buffer davranışı:
 *  - HT: current target (CT) buffer’ının İLK yarısı doldu.
 *  - TC: transfer bitti, donanım CT bitini diğer buffer’a geçirdi;
 *        yani tamamlanan buffer = CT'nin TERSİ, İKİNCİ yarı.
 */

#define OPT_LOG
void DMA2_Stream0_IRQHandler(void)
{

	/* HT? -> aktif CT buffer'ın 1. yarısı */
	if (LL_DMA_IsActiveFlag_HT0(DMA2))
	{
		LL_DMA_ClearFlag_HT0(DMA2);
		uint32_t ct = LL_DMA_GetCurrentTargetMem(DMA2, LL_DMA_STREAM_0); /* 0->M0, 1->M1 */
		struct adc_stream_block b = {
			.ptr = (ct == LL_DMA_CURRENTTARGETMEM0) ? &buf_m0[0] : &buf_m1[0],
			.count = BUF_SAMPLES / 2,
			.seq = g_seq++,
			.second_half = false,
			.buf_id = (ct == LL_DMA_CURRENTTARGETMEM0) ? 0 : 1,
		};
#if defined(OPT_LOG)
		(void)k_msgq_put(&adc_block_q, &b, K_NO_WAIT);
#else
		int ret = k_msgq_put(&adc_block_q, &b, K_NO_WAIT);
		if (ret < 0)
			LOG_INFO("An error occurred while adding Block to msgq err:%d." ret);
#endif
	}

	/* TC? -> tamamlanan buffer = (CT'nin tersi), 2. yarı */
	if (LL_DMA_IsActiveFlag_TC0(DMA2))
	{
		LL_DMA_ClearFlag_TC0(DMA2);
		uint32_t ct = LL_DMA_GetCurrentTargetMem(DMA2, LL_DMA_STREAM_0);
		uint8_t done_id = (ct == LL_DMA_CURRENTTARGETMEM0) ? 1 : 0; /* CT toggle sonrası CT yeni hedefi gösterir */
		struct adc_stream_block b = {
			.ptr = (done_id == 0) ? &buf_m0[BUF_SAMPLES / 2] : &buf_m1[BUF_SAMPLES / 2],
			.count = BUF_SAMPLES / 2,
			.seq = g_seq++,
			.second_half = true,
			.buf_id = done_id,
		};

#if defined(OPT_LOG)
		(void)k_msgq_put(&adc_block_q, &b, K_NO_WAIT);
#else
		if (ret < 0)
			LOG_INFO("An error occurred while adding Block to msgq err:%d." ret);
#endif
	}
}
