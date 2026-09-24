#include "stm32f1xx_hal.h"
#include <stdint.h>

/* =========================================================
 * HANDLE
 * ========================================================= */

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim3;
UART_HandleTypeDef huart1;

/* =========================================================
 * ADC BUFFER
 *
 * 100 mẫu ở 100 Hz = 1 giây
 * ========================================================= */

#define ADC_BUFFER_SIZE    100
#define ADC_HALF_SIZE       50

uint16_t adc_buffer[ADC_BUFFER_SIZE];

/* =========================================================
 * SAFE TRANSMIT BUFFER
 *
 * Khi DMA hoàn thành một nửa buffer,
 * copy sang đây để UART truyền.
 * ========================================================= */

uint16_t tx_half_buffer[ADC_HALF_SIZE];
uint16_t tx_full_buffer[ADC_HALF_SIZE];

/* =========================================================
 * FLAGS
 * ========================================================= */

volatile uint8_t half_ready = 0;
volatile uint8_t full_ready = 0;

/* =========================================================
 * RAM VECTOR TABLE
 *
 * STM32F103 có:
 *
 * 16 vector core
 * + 43 external interrupt
 * = 59 vector
 *
 * Căn 128 byte cho Cortex-M3.
 * ========================================================= */

#define VECTOR_COUNT 59

__attribute__((aligned(128)))
uint32_t ram_vector_table[VECTOR_COUNT];

/* =========================================================
 * PROTOTYPE
 * ========================================================= */

static void SystemClock_Config(void);

static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);

static void Setup_RAM_Vector_Table(void);

static void UART_SendString(const char *str);
static void UART_SendNumber(uint32_t number);

static void Send_ADC_Block(
    uint16_t *buffer,
    uint16_t count
);

static void Error_Handler(void);

/* =========================================================
 * SYSTICK
 * ========================================================= */

void SysTick_Handler(void)
{
    HAL_IncTick();
    HAL_SYSTICK_IRQHandler();
}

/* =========================================================
 * DMA1 CHANNEL1 IRQ
 *
 * IRQ number của DMA1 Channel1 = 11
 *
 * Handler này được gắn trực tiếp vào
 * RAM vector table.
 * ========================================================= */

void DMA1_Channel1_IRQHandler(void)
{
    uint32_t isr = DMA1->ISR;

    /* -----------------------------------------------------
     * HALF TRANSFER
     *
     * DMA đã ghi:
     * adc_buffer[0 ... 49]
     *
     * Nửa đầu an toàn.
     * ----------------------------------------------------- */

    if (isr & DMA_ISR_HTIF1)
    {
        uint16_t i;

        /* Xóa cờ Half Transfer */
        DMA1->IFCR = DMA_IFCR_CHTIF1;

        /*
         * Copy 50 mẫu sang buffer an toàn.
         */
        for (i = 0; i < ADC_HALF_SIZE; i++)
        {
            tx_half_buffer[i] =
                adc_buffer[i];
        }

        /*
         * Báo main có dữ liệu.
         */
        half_ready = 1;
    }

    /* -----------------------------------------------------
     * TRANSFER COMPLETE
     *
     * DMA đã ghi:
     * adc_buffer[50 ... 99]
     *
     * Nửa sau an toàn.
     * ----------------------------------------------------- */

    if (isr & DMA_ISR_TCIF1)
    {
        uint16_t i;

        /* Xóa cờ Transfer Complete */
        DMA1->IFCR = DMA_IFCR_CTCIF1;

        /*
         * Copy 50 mẫu sang buffer an toàn.
         */
        for (i = 0; i < ADC_HALF_SIZE; i++)
        {
            tx_full_buffer[i] =
                adc_buffer[ADC_HALF_SIZE + i];
        }

        /*
         * Báo main có dữ liệu.
         */
        full_ready = 1;
    }

    /* -----------------------------------------------------
     * TRANSFER ERROR
     * ----------------------------------------------------- */

    if (isr & DMA_ISR_TEIF1)
    {
        DMA1->IFCR = DMA_IFCR_CTEIF1;
    }
}

/* =========================================================
 * SETUP RAM VECTOR TABLE
 * ========================================================= */

static void Setup_RAM_Vector_Table(void)
{
    uint32_t i;

    /*
     * Tắt interrupt trong lúc thay vector.
     */
    __disable_irq();

    /*
     * Copy vector table hiện tại từ Flash
     * sang RAM.
     */
    for (i = 0; i < VECTOR_COUNT; i++)
    {
        ram_vector_table[i] =
            ((uint32_t *)FLASH_BASE)[i];
    }

    /*
     * DMA1 Channel1 có IRQ number = 11.
     *
     * Vector index:
     *
     * 16 + 11 = 27
     */
    ram_vector_table[
        16 + DMA1_Channel1_IRQn
    ] = (uint32_t)DMA1_Channel1_IRQHandler;

    /*
     * Chuyển vector table sang RAM.
     */
    SCB->VTOR =
        (uint32_t)ram_vector_table;

    /*
     * Đảm bảo CPU nhận vector table mới.
     */
    __DSB();
    __ISB();

    /*
     * Bật interrupt lại.
     */
    __enable_irq();
}

/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    HAL_Init();

    /*
     * Cấu hình vector DMA trong RAM
     * trước khi bật ADC + DMA.
     */
    Setup_RAM_Vector_Table();

    SystemClock_Config();

    MX_GPIO_Init();

    /*
     * UART
     */
    MX_USART1_UART_Init();

    UART_SendString("READY\n\r");

    /*
     * ADC
     */
    MX_ADC1_Init();

    /*
     * TIM3
     */
    MX_TIM3_Init();

    /* -----------------------------------------------------
     * ADC CALIBRATION
     * ----------------------------------------------------- */

    if (HAL_ADCEx_Calibration_Start(&hadc1)
        != HAL_OK)
    {
        UART_SendString(
            "ADC CAL ERROR\n\r"
        );

        Error_Handler();
    }

    UART_SendString("ADC OK\n\r");

    /* -----------------------------------------------------
     * ADC + DMA
     * ----------------------------------------------------- */

    if (HAL_ADC_Start_DMA(
            &hadc1,
            (uint32_t *)adc_buffer,
            ADC_BUFFER_SIZE
        ) != HAL_OK)
    {
        UART_SendString(
            "DMA START ERROR\n\r"
        );

        Error_Handler();
    }

    UART_SendString("DMA OK\n\r");

    /* -----------------------------------------------------
     * TIM3
     *
     * 100 Hz
     * Update -> TRGO
     * ----------------------------------------------------- */

    if (HAL_TIM_Base_Start(&htim3)
        != HAL_OK)
    {
        UART_SendString(
            "TIM START ERROR\n\r"
        );

        Error_Handler();
    }

    UART_SendString("TIM OK\n\r");

    /* =====================================================
     * MAIN LOOP
     * ===================================================== */

    while (1)
    {
        /* -------------------------------------------------
         * HALF TRANSFER
         *
         * 50 mẫu đầu
         * ------------------------------------------------- */

        if (half_ready)
        {
            half_ready = 0;

            UART_SendString(
                "HALF\n\r"
            );

            Send_ADC_Block(
                tx_half_buffer,
                ADC_HALF_SIZE
            );
        }

        /* -------------------------------------------------
         * TRANSFER COMPLETE
         *
         * 50 mẫu sau
         * ------------------------------------------------- */

        if (full_ready)
        {
            full_ready = 0;

            UART_SendString(
                "FULL\n\r"
            );

            Send_ADC_Block(
                tx_full_buffer,
                ADC_HALF_SIZE
            );
        }
    }
}

/* =========================================================
 * SEND ADC BLOCK
 * ========================================================= */

static void Send_ADC_Block(
    uint16_t *buffer,
    uint16_t count)
{
    uint16_t i;

    for (i = 0; i < count; i++)
    {
        /*
         * Giá trị ADC 0 ... 4095
         */
        UART_SendNumber(
            buffer[i]
        );

        /*
         * Mỗi giá trị một dòng.
         */
        UART_SendString(
            "\n\r"
        );
    }
}

/* =========================================================
 * UART SEND STRING
 * ========================================================= */

static void UART_SendString(
    const char *str)
{
    while (*str)
    {
        HAL_UART_Transmit(
            &huart1,
            (uint8_t *)str,
            1,
            HAL_MAX_DELAY
        );

        str++;
    }
}

/* =========================================================
 * UART SEND NUMBER
 *
 * Không dùng printf / snprintf.
 * ========================================================= */

static void UART_SendNumber(
    uint32_t number)
{
    char buffer[10];

    uint8_t index = 0;
    uint8_t i;

    /*
     * Số 0.
     */
    if (number == 0)
    {
        UART_SendString("0");
        return;
    }

    /*
     * Tách chữ số.
     */
    while (number > 0)
    {
        buffer[index] =
            (char)('0' + (number % 10));

        number /= 10;

        index++;
    }

    /*
     * Đảo lại và gửi.
     */
    for (i = 0; i < index; i++)
    {
        char c =
            buffer[index - 1 - i];

        HAL_UART_Transmit(
            &huart1,
            (uint8_t *)&c,
            1,
            HAL_MAX_DELAY
        );
    }
}

/* =========================================================
 * SYSTEM CLOCK
 *
 * HSI = 8 MHz
 * ========================================================= */

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSI;

    RCC_OscInitStruct.HSIState =
        RCC_HSI_ON;

    RCC_OscInitStruct.HSICalibrationValue =
        RCC_HSICALIBRATION_DEFAULT;

    RCC_OscInitStruct.PLL.PLLState =
        RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(
            &RCC_OscInitStruct
        ) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_HSI;

    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_SYSCLK_DIV1;

    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_HCLK_DIV1;

    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_0
        ) != HAL_OK)
    {
        Error_Handler();
    }
}

/* =========================================================
 * GPIO
 *
 * PA0 = ADC1_IN0
 * ========================================================= */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin =
        GPIO_PIN_0;

    GPIO_InitStruct.Mode =
        GPIO_MODE_ANALOG;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct
    );
}

/* =========================================================
 * ADC1
 *
 * PA0 = ADC1_IN0
 *
 * Trigger = TIM3 TRGO
 * ========================================================= */

static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc1.Instance =
        ADC1;

    /*
     * Chỉ dùng một channel.
     */
    hadc1.Init.ScanConvMode =
        ADC_SCAN_DISABLE;

    /*
     * Không continuous.
     *
     * Mỗi TIM3 trigger:
     * 1 ADC conversion.
     */
    hadc1.Init.ContinuousConvMode =
        DISABLE;

    hadc1.Init.DiscontinuousConvMode =
        DISABLE;

    /*
     * TIM3 TRGO.
     */
    hadc1.Init.ExternalTrigConv =
        ADC_EXTERNALTRIGCONV_T3_TRGO;

    /*
     * 12 bit.
     */
    hadc1.Init.DataAlign =
        ADC_DATAALIGN_RIGHT;

    hadc1.Init.NbrOfConversion =
        1;

    if (HAL_ADC_Init(&hadc1)
        != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * ADC1 Channel 0 = PA0.
     */
    sConfig.Channel =
        ADC_CHANNEL_0;

    sConfig.Rank =
        ADC_REGULAR_RANK_1;

    /*
     * Sampling time dài.
     */
    sConfig.SamplingTime =
        ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(
            &hadc1,
            &sConfig
        ) != HAL_OK)
    {
        Error_Handler();
    }
}

/* =========================================================
 * ADC MSP
 *
 * ADC1 -> DMA1 Channel1
 * ========================================================= */

void HAL_ADC_MspInit(
    ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        /*
         * GPIOA
         */
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /*
         * ADC1
         */
        __HAL_RCC_ADC1_CLK_ENABLE();

        /*
         * DMA1
         */
        __HAL_RCC_DMA1_CLK_ENABLE();

        /*
         * DMA1 Channel1
         */
        hdma_adc1.Instance =
            DMA1_Channel1;

        /*
         * Peripheral -> Memory
         */
        hdma_adc1.Init.Direction =
            DMA_PERIPH_TO_MEMORY;

        /*
         * ADC DR không đổi.
         */
        hdma_adc1.Init.PeriphInc =
            DMA_PINC_DISABLE;

        /*
         * RAM tăng sau mỗi sample.
         */
        hdma_adc1.Init.MemInc =
            DMA_MINC_ENABLE;

        /*
         * ADC data 16 bit.
         */
        hdma_adc1.Init.PeriphDataAlignment =
            DMA_PDATAALIGN_HALFWORD;

        /*
         * RAM data 16 bit.
         */
        hdma_adc1.Init.MemDataAlignment =
            DMA_MDATAALIGN_HALFWORD;

        /*
         * Circular.
         */
        hdma_adc1.Init.Mode =
            DMA_CIRCULAR;

        /*
         * Priority cao.
         */
        hdma_adc1.Init.Priority =
            DMA_PRIORITY_HIGH;

        if (HAL_DMA_Init(
                &hdma_adc1
            ) != HAL_OK)
        {
            Error_Handler();
        }

        /*
         * Liên kết DMA với ADC.
         */
        __HAL_LINKDMA(
            hadc,
            DMA_Handle,
            hdma_adc1
        );

        /*
         * Enable DMA1 Channel1 interrupt.
         */
        HAL_NVIC_SetPriority(
            DMA1_Channel1_IRQn,
            0,
            0
        );

        HAL_NVIC_EnableIRQ(
            DMA1_Channel1_IRQn
        );
    }
}

/* =========================================================
 * TIM3
 *
 * HSI = 8 MHz
 *
 * 8,000,000 / 8000
 * = 1000 Hz
 *
 * 1000 / 10
 * = 100 Hz
 *
 * Update Event -> TRGO
 * ========================================================= */

static void MX_TIM3_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim3.Instance =
        TIM3;

    /*
     * 8 MHz / (7999 + 1)
     * = 1000 Hz
     */
    htim3.Init.Prescaler =
        7999;

    htim3.Init.CounterMode =
        TIM_COUNTERMODE_UP;

    /*
     * 1000 Hz / (9 + 1)
     * = 100 Hz
     */
    htim3.Init.Period =
        9;

    htim3.Init.ClockDivision =
        TIM_CLOCKDIVISION_DIV1;

    htim3.Init.AutoReloadPreload =
        TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(
            &htim3
        ) != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * Update Event -> TRGO.
     */
    sMasterConfig.MasterOutputTrigger =
        TIM_TRGO_UPDATE;

    sMasterConfig.MasterSlaveMode =
        TIM_MASTERSLAVEMODE_DISABLE;

    if (HAL_TIMEx_MasterConfigSynchronization(
            &htim3,
            &sMasterConfig
        ) != HAL_OK)
    {
        Error_Handler();
    }
}

/* =========================================================
 * TIM3 MSP
 * ========================================================= */

void HAL_TIM_Base_MspInit(
    TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
}

/* =========================================================
 * UART1
 *
 * PA9  = TX
 * PA10 = RX
 *
 * 115200 8N1
 * ========================================================= */

static void MX_USART1_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    __HAL_RCC_USART1_CLK_ENABLE();

    /*
     * PA9 = TX
     */
    GPIO_InitStruct.Pin =
        GPIO_PIN_9;

    GPIO_InitStruct.Mode =
        GPIO_MODE_AF_PP;

    GPIO_InitStruct.Speed =
        GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct
    );

    /*
     * PA10 = RX
     */
    GPIO_InitStruct.Pin =
        GPIO_PIN_10;

    GPIO_InitStruct.Mode =
        GPIO_MODE_INPUT;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct
    );

    /*
     * UART config.
     */
    huart1.Instance =
        USART1;

    huart1.Init.BaudRate =
        115200;

    huart1.Init.WordLength =
        UART_WORDLENGTH_8B;

    huart1.Init.StopBits =
        UART_STOPBITS_1;

    huart1.Init.Parity =
        UART_PARITY_NONE;

    huart1.Init.Mode =
        UART_MODE_TX_RX;

    huart1.Init.HwFlowCtl =
        UART_HWCONTROL_NONE;

    huart1.Init.OverSampling =
        UART_OVERSAMPLING_16;

    if (HAL_UART_Init(
            &huart1
        ) != HAL_OK)
    {
        Error_Handler();
    }
}

/* =========================================================
 * ERROR HANDLER
 * ========================================================= */

static void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}
