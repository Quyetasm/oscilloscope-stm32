/**
 * @file    main.h
 * @brief   Board/peripheral handles and top-level prototypes.
 */
#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Display SPI handle (used by ili9488.c). */
extern SPI_HandleTypeDef hspi1;

/** @brief Configure the system clock (HSE 25 MHz -> 100 MHz). */
void SystemClock_Config(void);

/** @brief Fatal error trap. */
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
