/**
 * @file    main.c
 * @brief   Entry point and board bring-up for the STM32F411 oscilloscope.
 *
 * Bring-up order:
 *   1. HAL_Init + SystemClock_Config (100 MHz)
 *   2. GPIO (display control + SPI alternate function)
 *   3. SPI1 (display)
 *   4. ADC1 + DMA2 + TIM3 (capture)         -> ADC_DMA_Init()
 *   5. ILI9488_Init()
 *   6. Encoder_Init() / Trigger_Init()
 *   7. Oscilloscope_Init()
 *   8. while(1) Oscilloscope_Run()
 */
#include "main.h"
#include "ili9488.h"
#include "adc_dma.h"
#include "encoder.h"
#include "trigger.h"
#include "oscilloscope.h"

SPI_HandleTypeDef hspi1;

/** @brief Initialise GPIO for the display control lines and SPI1 pins. */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Display control: PB0=DC, PB1=CS, PB2=RST as push-pull outputs. */
    HAL_GPIO_WritePin(GPIOB, ILI9488_DC_PIN | ILI9488_CS_PIN | ILI9488_RST_PIN, GPIO_PIN_SET);
    gpio.Pin   = ILI9488_DC_PIN | ILI9488_CS_PIN | ILI9488_RST_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* SPI1: PA5=SCK, PA6=MISO, PA7=MOSI on AF5. */
    gpio.Pin       = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/** @brief Configure SPI1 as master, mode 0, fPCLK/2 (~50 MHz). */
static void MX_SPI1_Init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;   /* CPOL = 0 */
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;    /* CPHA = 0 */
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; /* fPCLK2/2 */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10U;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        Error_Handler();
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_SPI1_Init();
    ADC_DMA_Init();

    ILI9488_Init();
    Encoder_Init();
    Trigger_Init();
    Oscilloscope_Init();

    while (1) {
        Oscilloscope_Run();
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc_init = {0};
    RCC_ClkInitTypeDef clk_init = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* HSE 25 MHz (Black Pill) -> PLL -> 100 MHz SYSCLK.
     * VCO = 25/PLLM*PLLN = 25/25*200 = 200 MHz; SYSCLK = 200/PLLP(2) = 100 MHz. */
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc_init.HSEState       = RCC_HSE_ON;
    osc_init.PLL.PLLState   = RCC_PLL_ON;
    osc_init.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc_init.PLL.PLLM       = 25U;
    osc_init.PLL.PLLN       = 200U;
    osc_init.PLL.PLLP       = 2U;
    osc_init.PLL.PLLQ       = 4U;
    if (HAL_RCC_OscConfig(&osc_init) != HAL_OK) {
        Error_Handler();
    }

    /* AHB = 100 MHz, APB1 = 50 MHz (timers 100 MHz), APB2 = 100 MHz. */
    clk_init.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                              RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk_init.APB1CLKDivider = RCC_HCLK_DIV2;
    clk_init.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_3) != HAL_OK) {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        /* Trap: halt with interrupts disabled. */
    }
}

/*
 * NOTE: SysTick_Handler is intentionally NOT defined here. In a CubeIDE/CubeMX
 * project it is generated in stm32f4xx_it.c (and already calls HAL_IncTick()),
 * so defining it here too would cause a "multiple definition" link error.
 * The ADC1 / DMA2_Stream0 interrupt handlers live in adc_dma.c because those
 * interrupts are enabled in code rather than via CubeMX; if you later enable
 * them in the .ioc, remove the duplicates from stm32f4xx_it.c the same way.
 */
