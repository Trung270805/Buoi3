#include "stm32f1xx_hal.h"
#include <stdint.h>

volatile uint32_t button_count = 0;
volatile uint32_t last_press_time = 0;

volatile uint8_t dma_busy = 0;

uint8_t tx_buffer[32];

/* ================= QUEUE ================= */

#define QUEUE_SIZE 8

volatile uint32_t message_queue[QUEUE_SIZE];
volatile uint8_t queue_head = 0;
volatile uint8_t queue_tail = 0;

/* ================= VECTOR TABLE ================= */

uint32_t vector_table[64]
    __attribute__((aligned(256)));

/* ================= PROTOTYPE ================= */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_Init(void);

static void Queue_Push(uint32_t value);
static uint8_t Queue_IsEmpty(void);

static void Start_DMA(uint32_t value);

static void RelocateVectorTable(void);

static void Error_Handler(void);

/* ================= SYSTICK ================= */

void SysTick_Handler(void)
{
    HAL_IncTick();
    HAL_SYSTICK_IRQHandler();
}

/* ================= EXTI0 ================= */

void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

/* ================= BUTTON CALLBACK ================= */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0)
    {
        uint32_t now = HAL_GetTick();

        /* Chống dội nút 100 ms */
        if ((now - last_press_time) < 100)
        {
            return;
        }

        last_press_time = now;

        /* Tăng giá trị nút */
        button_count++;

        /* Đưa giá trị vào queue */
        Queue_Push(button_count);
    }
}

/* ================= DMA1 CHANNEL4 IRQ ================= */

void DMA1_Channel4_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TCIF4)
    {
        /* Tắt DMA Channel 4 */
        DMA1_Channel4->CCR &= ~DMA_CCR_EN;

        /* Xóa cờ DMA */
        DMA1->IFCR =
            DMA_IFCR_CTCIF4 |
            DMA_IFCR_CHTIF4 |
            DMA_IFCR_CTEIF4;

        /* DMA đã truyền xong */
        dma_busy = 0;
    }
}

/* ================= MAIN ================= */

int main(void)
{
    HAL_Init();

    SystemClock_Config();

    RelocateVectorTable();

    MX_GPIO_Init();

    MX_USART1_Init();

    while (1)
    {
        /*
         * Nếu DMA rảnh và queue có dữ liệu
         */
        if (!dma_busy && !Queue_IsEmpty())
        {
            uint32_t value;

            value =
                message_queue[queue_head];

            queue_head =
                (queue_head + 1) % QUEUE_SIZE;

            Start_DMA(value);
        }
    }
}

/* ================= VECTOR TABLE ================= */

static void RelocateVectorTable(void)
{
    uint32_t i;

    /* Copy vector table từ Flash sang RAM */
    for (i = 0; i < 64; i++)
    {
        vector_table[i] =
            ((uint32_t *)0x08000000)[i];
    }

    /*
     * EXTI0
     * IRQ = 6
     * Vector = 16 + 6 = 22
     */
    vector_table[22] =
        (uint32_t)EXTI0_IRQHandler;

    /*
     * DMA1 Channel4
     * IRQ = 14
     * Vector = 16 + 14 = 30
     */
    vector_table[30] =
        (uint32_t)DMA1_Channel4_IRQHandler;

    /* Chuyển VTOR sang RAM */
    SCB->VTOR =
        (uint32_t)vector_table;

    __DSB();
    __ISB();
}

/* ================= QUEUE ================= */

static uint8_t Queue_IsEmpty(void)
{
    return queue_head == queue_tail;
}

static void Queue_Push(uint32_t value)
{
    uint8_t next;

    next =
        (queue_tail + 1) %
        QUEUE_SIZE;

    if (next != queue_head)
    {
        message_queue[queue_tail] =
            value;

        queue_tail = next;
    }
}

/* ================= START DMA ================= */

static void Start_DMA(uint32_t value)
{
    uint8_t i = 0;

    char temp[12];
    uint8_t j = 0;

    /*
     * Format:
     *
     * <MT02><01>:BTN:1\n\r
     * <MT02><01>:BTN:2\n\r
     * ...
     */

    /* Prefix */
    tx_buffer[i++] = '<';
    tx_buffer[i++] = 'M';
    tx_buffer[i++] = 'T';
    tx_buffer[i++] = '0';
    tx_buffer[i++] = '2';
    tx_buffer[i++] = '>';

    tx_buffer[i++] = '<';
    tx_buffer[i++] = '0';
    tx_buffer[i++] = '1';
    tx_buffer[i++] = '>';

    tx_buffer[i++] = ':';

    tx_buffer[i++] = 'B';
    tx_buffer[i++] = 'T';
    tx_buffer[i++] = 'N';

    tx_buffer[i++] = ':';

    /* Đổi số thành chuỗi */
    if (value == 0)
    {
        temp[j++] = '0';
    }
    else
    {
        while (value > 0)
        {
            temp[j++] =
                '0' + (value % 10);

            value /= 10;
        }
    }

    /* Đảo chuỗi số */
    while (j > 0)
    {
        tx_buffer[i++] =
            temp[--j];
    }

    /* Xuống dòng */
    tx_buffer[i++] = '\n';
    tx_buffer[i++] = '\r';

    /* DMA đang bận */
    dma_busy = 1;

    /*
     * Tắt Channel 4 trước khi cấu hình
     */
    DMA1_Channel4->CCR &=
        ~DMA_CCR_EN;

    /* Xóa cờ DMA Channel 4 */
    DMA1->IFCR =
        DMA_IFCR_CGIF4;

    /*
     * Địa chỉ thanh ghi USART1->DR
     */
    DMA1_Channel4->CPAR =
        (uint32_t)&USART1->DR;

    /*
     * Địa chỉ buffer
     */
    DMA1_Channel4->CMAR =
        (uint32_t)tx_buffer;

    /*
     * Số byte cần truyền
     */
    DMA1_Channel4->CNDTR = i;

    /*
     * Cấu hình DMA:
     *
     * MINC  = tăng địa chỉ bộ nhớ
     * DIR   = Memory -> Peripheral
     * TCIE  = ngắt khi truyền xong
     * PL    = ưu tiên cao
     */
    DMA1_Channel4->CCR =
        DMA_CCR_MINC |
        DMA_CCR_DIR |
        DMA_CCR_TCIE |
        DMA_CCR_PL_1;

    /*
     * Cho USART1 yêu cầu DMA TX
     */
    USART1->CR3 |=
        USART_CR3_DMAT;

    /* Bật DMA Channel 4 */
    DMA1_Channel4->CCR |=
        DMA_CCR_EN;
}

/* ================= CLOCK ================= */

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
            &RCC_OscInitStruct) != HAL_OK)
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
            FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ================= GPIO ================= */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /*
     * PA0 = nút nhấn
     * Pull-up nội
     *
     * Không nhấn = 1
     * Nhấn = 0
     */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0;

    GPIO_InitStruct.Mode =
        GPIO_MODE_IT_FALLING;

    GPIO_InitStruct.Pull =
        GPIO_PULLUP;

    HAL_GPIO_Init(
        GPIOA,
        &GPIO_InitStruct
    );

    /* EXTI0 */
    HAL_NVIC_SetPriority(
        EXTI0_IRQn,
        1,
        0
    );

    HAL_NVIC_EnableIRQ(
        EXTI0_IRQn
    );
}

/* ================= USART1 ================= */

static void MX_USART1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_DMA1_CLK_ENABLE();

    /*
     * PA9 = USART1 TX
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
     * USART1
     *
     * HSI = 8 MHz
     * Baudrate = 115200
     */
    USART1->BRR = 69;

    /*
     * Enable:
     * UE = USART
     * TE = TX
     */
    USART1->CR1 =
        USART_CR1_UE |
        USART_CR1_TE;

    USART1->CR2 = 0;

    USART1->CR3 = 0;

    /*
     * Enable DMA Channel 4 interrupt
     */
    HAL_NVIC_SetPriority(
        DMA1_Channel4_IRQn,
        0,
        0
    );

    HAL_NVIC_EnableIRQ(
        DMA1_Channel4_IRQn
    );
}

/* ================= ERROR ================= */

static void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}
