# app/main

target_sources(app PRIVATE
  src/main.c
  src/app_adc.c
      )

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

if(NOT CONFIG_SOC_SERIES_STM32F0X)
  target_sources(app PRIVATE src/sd_fs.c)
endif()
