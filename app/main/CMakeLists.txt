# app/main

target_sources(app PRIVATE
  src/main.c
      )

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

if(CONFIG_SOC_SERIES_STM32F0X OR CONFIG_TEST)
  target_sources(app PRIVATE src/app_adc_f0_test.c)
else()
  target_sources(app PRIVATE src/app_adc_f4_dma.c)
endif()