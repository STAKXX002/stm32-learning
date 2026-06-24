/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */
// ==================== DMA RX BUFFER ====================
#define RX_BUF_SIZE 128
uint8_t rxBuf[RX_BUF_SIZE];
uint32_t rxReadPos = 0;

// ==================== CONSTANTS ====================
#define STEPS_PER_MM     800
#define CPR              4000
#define STOP_DEADBAND    5.0f
#define MAX_SPEED        800.0f
#define ACCEL            2000.0f
#define DT               0.0001f

#define Kp               0.55f
#define Kd               0.14f
#define Kp_slave         0.7f

// ==================== MODE ====================
typedef enum { MODE_IDLE, MODE_X, MODE_Z } Mode;
volatile Mode currentMode = MODE_IDLE;

// ==================== Z AXIS ====================
volatile int32_t z1Enc = 0, z2Enc = 0;
uint16_t z1Last = 0, z2Last = 0;

float targetStepsZ   = 0.0f;
float z1CmdPos       = 0.0f, z2CmdPos = 0.0f;
float z1LastErr      = 0.0f, z2LastErr = 0.0f;
volatile bool runningZ         = false;
volatile bool justStartedMoveZ = false;
volatile int  settleGuardZ     = 0;

// ==================== X AXIS ====================
volatile int32_t xEnc = 0;
uint16_t xLast = 0;

float targetStepsX   = 0.0f;
float xCmdPos        = 0.0f;
float xLastErr       = 0.0f;
volatile bool runningX         = false;
volatile bool justStartedMoveX = false;
volatile int  settleGuardX     = 0;

// ==================== STEP GENERATION ====================
volatile uint32_t z1StepInterval = 0;
volatile uint32_t z2StepInterval = 0;
volatile uint32_t xStepInterval  = 0;

// ==================== QUEUE ====================
#define MAX_QUEUE 20
char cmdQueue[MAX_QUEUE][32];
int queueHead = 0, queueTail = 0;
char originalQueue[MAX_QUEUE][32];
int originalSize = 0;
volatile bool sequenceRunning  = false;
int sequenceRepeatLeft = 0;

// ==================== DEBUG ====================
volatile bool debugPrintZ = false;
volatile bool debugPrintX = false;

typedef struct { float actual, target, err, vel; } DbgX;
typedef struct { float z1, z2, target, err1, followErr, vel1, vel2; } DbgZ;

volatile DbgX dbgX;
volatile DbgZ dbgZ;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */
void enableZ(bool en);
void enableX(bool en);
void updateEncoders(void);
void runZ_ISR(void);
void runX_ISR(void);
void setStepVelocity(TIM_HandleTypeDef *htim, uint32_t channel,
                     GPIO_TypeDef *dirPort, uint16_t dirPin,
                     float vel, volatile uint32_t *interval);
void handleSerial(void);
void executeCommand(const char *cmd);
void runSequence(void);
bool queueEmpty(void);
bool queueFull(void);
void enqueue(const char *cmd);
const char *dequeue(void);
void reloadQueue(void);
void uartPrint(const char *s);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  // Start encoders
  HAL_TIM_Encoder_Start(&htim1, TIM_CHANNEL_ALL);  // Z1
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);  // Z2
  HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);  // X

  // Start step output compare with interrupt
  HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_1);  // Z1 STEP
  HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_2);  // Z2 STEP
  HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_3);  // X  STEP

  // Start control loop timer with interrupt
  HAL_TIM_Base_Start_IT(&htim6);

  // Arm UART DMA receive
  HAL_UART_Receive_DMA(&huart2, rxBuf, RX_BUF_SIZE);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    handleSerial();
    runSequence();

    if (debugPrintZ)
    {
        debugPrintZ = false;
        char buf[120];
        snprintf(buf, sizeof(buf),
            "[Z] z1:%.2f z2:%.2f tgt:%.2f err:%.2f fErr:%.2f v1:%.2f v2:%.2f skew:%.4f\r\n",
            (double)dbgZ.z1, (double)dbgZ.z2, (double)dbgZ.target,
            (double)dbgZ.err1, (double)dbgZ.followErr,
            (double)dbgZ.vel1, (double)dbgZ.vel2,
            (double)((dbgZ.z1 - dbgZ.z2) / STEPS_PER_MM));
        uartPrint(buf);
    }

    if (debugPrintX)
    {
        debugPrintX = false;
        char buf[80];
        snprintf(buf, sizeof(buf),
            "[X] pos:%.2f tgt:%.2f err:%.2f vel:%.2f\r\n",
            (double)dbgX.actual, (double)dbgX.target,
            (double)dbgX.err,    (double)dbgX.vel);
        uartPrint(buf);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TOGGLE;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim4, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 89;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 99;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
  /* DMA1_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, Z1_DIR_Pin|Z2_DIR_Pin|X_DIR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, Z1_EN_Pin|Z2_EN_Pin|X_EN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : Z1_DIR_Pin Z1_EN_Pin Z2_DIR_Pin Z2_EN_Pin
                           X_DIR_Pin X_EN_Pin */
  GPIO_InitStruct.Pin = Z1_DIR_Pin|Z1_EN_Pin|Z2_DIR_Pin|Z2_EN_Pin
                          |X_DIR_Pin|X_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void uartPrint(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 10);
    HAL_UART_Transmit(&huart2, (uint8_t*)s, strlen(s), 10);
}

void enableZ(bool en)
{
    HAL_GPIO_WritePin(Z1_EN_GPIO_Port, Z1_EN_Pin,
                      en ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(Z2_EN_GPIO_Port, Z2_EN_Pin,
                      en ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void enableX(bool en)
{
    HAL_GPIO_WritePin(X_EN_GPIO_Port, X_EN_Pin,
                      en ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void updateEncoders(void)
{
    uint16_t c1 = (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);
    uint16_t c2 = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
    uint16_t cx = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);

    int32_t d1 = (int32_t)(int16_t)(c1 - z1Last);
    int32_t d2 = (int32_t)(int16_t)(c2 - z2Last);
    int32_t dx = (int32_t)(int16_t)(cx - xLast);

    if (d1 > -1000 && d1 < 1000) z1Enc += d1;
    if (d2 > -1000 && d2 < 1000) z2Enc += d2;
    if (dx > -1000 && dx < 1000) xEnc  += dx;

    z1Last = c1;
    z2Last = c2;
    xLast  = cx;
}

void setStepVelocity(TIM_HandleTypeDef *htim, uint32_t channel,
                     GPIO_TypeDef *dirPort, uint16_t dirPin,
                     float vel, volatile uint32_t *interval)
{
    if (vel >= 0)
        HAL_GPIO_WritePin(dirPort, dirPin, GPIO_PIN_SET);
    else
        HAL_GPIO_WritePin(dirPort, dirPin, GPIO_PIN_RESET);

    float absVel = vel < 0 ? -vel : vel;

    if (absVel < 1.0f)
    {
        *interval = 0xFFFFFFFF;
        return;
    }

    uint32_t ticks = (uint32_t)(90000000.0f / absVel);
    *interval = ticks;

    uint32_t current = __HAL_TIM_GET_COMPARE(htim, channel);
    __HAL_TIM_SET_COMPARE(htim, channel, current + ticks);
}

void runZ_ISR(void)
{
    if (!runningZ || currentMode != MODE_Z) return;

    const float stepPerEnc = (float)STEPS_PER_MM / (float)CPR;

    float z1Actual = z1Enc * stepPerEnc;
    float z2Actual = z2Enc * stepPerEnc;
    float err1     = targetStepsZ - z1Actual;

    float vff = sqrtf(2.0f * ACCEL * (err1 < 0 ? -err1 : err1));
    if (err1 < 0) vff = -vff;

    float d1  = (err1 - z1LastErr) / DT;
    z1LastErr = err1;

    float vel1 = vff + Kp * err1 + Kd * d1;
    if (vel1 >  MAX_SPEED) vel1 =  MAX_SPEED;
    if (vel1 < -MAX_SPEED) vel1 = -MAX_SPEED;

    float followErr = z1Actual - z2Actual;
    float d2        = (followErr - z2LastErr) / DT;
    z2LastErr       = followErr;

    float vel2 = vel1 + Kp_slave * followErr + Kd * d2;
    if (vel2 >  MAX_SPEED) vel2 =  MAX_SPEED;
    if (vel2 < -MAX_SPEED) vel2 = -MAX_SPEED;

    z1CmdPos += vel1 * DT;
    z2CmdPos += vel2 * DT;

    setStepVelocity(&htim2, TIM_CHANNEL_1,
                    Z1_DIR_GPIO_Port, Z1_DIR_Pin, vel1, &z1StepInterval);
    setStepVelocity(&htim2, TIM_CHANNEL_2,
                    Z2_DIR_GPIO_Port, Z2_DIR_Pin, vel2, &z2StepInterval);

    static uint32_t dbgCnt = 0;
    if (++dbgCnt >= 1000) {
        dbgCnt = 0;
        dbgZ.z1 = z1Actual; dbgZ.z2 = z2Actual;
        dbgZ.target = targetStepsZ;
        dbgZ.err1 = err1; dbgZ.followErr = followErr;
        dbgZ.vel1 = vel1; dbgZ.vel2 = vel2;
        debugPrintZ = true;
    }

    if (justStartedMoveZ) {
        if (++settleGuardZ >= 100) justStartedMoveZ = false;
        return;
    }

    if (err1 > -STOP_DEADBAND && err1 < STOP_DEADBAND &&
        followErr > -STOP_DEADBAND && followErr < STOP_DEADBAND &&
        vel1 > -5.0f && vel1 < 5.0f)
    {
        setStepVelocity(&htim2, TIM_CHANNEL_1,
                        Z1_DIR_GPIO_Port, Z1_DIR_Pin, 0, &z1StepInterval);
        setStepVelocity(&htim2, TIM_CHANNEL_2,
                        Z2_DIR_GPIO_Port, Z2_DIR_Pin, 0, &z2StepInterval);
        runningZ    = false;
        enableZ(false);
        currentMode = MODE_IDLE;
        uartPrint("Z OK\r\n");
    }
}

void runX_ISR(void)
{
    if (!runningX || currentMode != MODE_X) return;

    const float stepPerEnc = (float)STEPS_PER_MM / (float)CPR;

    float xActual = xEnc * stepPerEnc;
    float err     = targetStepsX - xActual;

    float vff = sqrtf(2.0f * ACCEL * (err < 0 ? -err : err));
    if (err < 0) vff = -vff;

    float d  = (err - xLastErr) / DT;
    xLastErr = err;

    float vel = vff + Kp * err + Kd * d;
    if (vel >  MAX_SPEED) vel =  MAX_SPEED;
    if (vel < -MAX_SPEED) vel = -MAX_SPEED;

    xCmdPos += vel * DT;

    setStepVelocity(&htim2, TIM_CHANNEL_3,
                    X_DIR_GPIO_Port, X_DIR_Pin, vel, &xStepInterval);

    static uint32_t dbgCnt = 0;
    if (++dbgCnt >= 1000) {
        dbgCnt = 0;
        dbgX.actual = xActual;
        dbgX.target = targetStepsX;
        dbgX.err    = err;
        dbgX.vel    = vel;
        debugPrintX = true;
    }

    if (justStartedMoveX) {
        if (++settleGuardX >= 100) justStartedMoveX = false;
        return;
    }

    if (err > -STOP_DEADBAND && err < STOP_DEADBAND &&
        vel > -5.0f && vel < 5.0f)
    {
        setStepVelocity(&htim2, TIM_CHANNEL_3,
                        X_DIR_GPIO_Port, X_DIR_Pin, 0, &xStepInterval);
        runningX    = false;
        enableX(false);
        currentMode = MODE_IDLE;
        uartPrint("X OK\r\n");
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM6) return;
    updateEncoders();
    runZ_ISR();
    runX_ISR();
}

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) return;

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1,
            __HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_1) + z1StepInterval);

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2,
            __HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_2) + z2StepInterval);

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3)
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3,
            __HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_3) + xStepInterval);
}

bool queueEmpty(void) { return queueHead == queueTail; }
bool queueFull(void)  { return ((queueTail + 1) % MAX_QUEUE) == queueHead; }

void enqueue(const char *cmd)
{
    if (queueFull()) { uartPrint("QUEUE FULL\r\n"); return; }
    strncpy(cmdQueue[queueTail], cmd, 31);
    queueTail = (queueTail + 1) % MAX_QUEUE;
}

const char *dequeue(void)
{
    if (queueEmpty()) return "";
    const char *cmd = cmdQueue[queueHead];
    queueHead = (queueHead + 1) % MAX_QUEUE;
    return cmd;
}

void reloadQueue(void)
{
    queueHead = queueTail = 0;
    for (int i = 0; i < originalSize; i++)
        enqueue(originalQueue[i]);
}

void executeCommand(const char *cmd)
{
    if (cmd[0] == 'Z')
    {
        enableX(false);
        runningX = false;
        enableZ(true);

        targetStepsZ = strtof(cmd + 1, NULL) * STEPS_PER_MM;

        const float sp = (float)STEPS_PER_MM / (float)CPR;
        z1CmdPos  = z1Enc * sp;
        z2CmdPos  = z2Enc * sp;
        z1LastErr = 0;
        z2LastErr = 0;

        runningZ         = true;
        justStartedMoveZ = true;
        settleGuardZ     = 0;
        currentMode      = MODE_Z;

        uartPrint("RUN Z\r\n");
    }
    else if (cmd[0] == 'G')
    {
        enableZ(false);
        runningZ = false;
        enableX(true);

        targetStepsX = strtof(cmd + 1, NULL) * STEPS_PER_MM;

        const float sp = (float)STEPS_PER_MM / (float)CPR;
        xCmdPos   = xEnc * sp;
        xLastErr  = 0;

        runningX         = true;
        justStartedMoveX = true;
        settleGuardX     = 0;
        currentMode      = MODE_X;

        uartPrint("RUN X\r\n");
    }
}

void runSequence(void)
{
    if (!sequenceRunning) return;
    if (currentMode != MODE_IDLE) return;

    if (!queueEmpty())
    {
        const char *next = dequeue();
        uartPrint("NEXT: ");
        uartPrint(next);
        uartPrint("\r\n");
        executeCommand(next);
    }
    else
    {
        sequenceRepeatLeft--;
        if (sequenceRepeatLeft > 0)
        {
            uartPrint("REPEAT\r\n");
            reloadQueue();
        }
        else
        {
            sequenceRunning = false;
            uartPrint("SEQUENCE DONE\r\n");
        }
    }
}

void handleSerial(void)
{
    uint32_t writePos = RX_BUF_SIZE -
                        __HAL_DMA_GET_COUNTER(huart2.hdmarx);

    while (rxReadPos != writePos)
    {
        uint8_t b = rxBuf[rxReadPos];
        rxReadPos = (rxReadPos + 1) % RX_BUF_SIZE;

        // Temporary debug — echo every raw byte back
        HAL_UART_Transmit(&huart2, &b, 1, 10);

        static char lineBuf[64];
        static uint32_t lineLen = 0;

        if (b == '\n' || b == '\r')
        {
            if (lineLen == 0) continue;
            lineBuf[lineLen] = '\0';
            lineLen = 0;

            if (strncmp(lineBuf, "SEQ", 3) == 0)
            {
                char *p = lineBuf + 3;
                while (*p == ' ') p++;

                char *lastComma = strrchr(p, ',');
                if (!lastComma) { uartPrint("INVALID SEQ\r\n"); continue; }

                int repeats = atoi(lastComma + 1);
                *lastComma = '\0';

                queueHead = queueTail = 0;
                originalSize = 0;

                char *tok = strtok(p, ",");
                while (tok)
                {
                    enqueue(tok);
                    strncpy(originalQueue[originalSize++], tok, 31);
                    tok = strtok(NULL, ",");
                }

                sequenceRepeatLeft = repeats;
                sequenceRunning    = true;
                uartPrint("SEQUENCE LOADED\r\n");
            }
            else if (lineBuf[0] == 'R')
            {
                enableZ(false);
                enableX(false);

                setStepVelocity(&htim2, TIM_CHANNEL_1,
                    Z1_DIR_GPIO_Port, Z1_DIR_Pin, 0, &z1StepInterval);
                setStepVelocity(&htim2, TIM_CHANNEL_2,
                    Z2_DIR_GPIO_Port, Z2_DIR_Pin, 0, &z2StepInterval);
                setStepVelocity(&htim2, TIM_CHANNEL_3,
                    X_DIR_GPIO_Port,  X_DIR_Pin,  0, &xStepInterval);

                runningX = runningZ = false;
                currentMode = MODE_IDLE;
                queueHead = queueTail = 0;
                originalSize = 0;
                sequenceRunning = false;
                uartPrint("RESET\r\n");
            }
            else if (strncmp(lineBuf, "STATUS", 6) == 0)
            {
                char buf[80];
                const float sp = (float)STEPS_PER_MM / (float)CPR;
                snprintf(buf, sizeof(buf),
                    "Z1=%.2f Z2=%.2f X=%.2f mode=%d\r\n",
                    (double)(z1Enc * sp / STEPS_PER_MM),
                    (double)(z2Enc * sp / STEPS_PER_MM),
                    (double)(xEnc  * sp / STEPS_PER_MM),
                    (int)currentMode);
                uartPrint(buf);
            }
            else
            {
                executeCommand(lineBuf);
            }
        }
        else
        {
            if (lineLen < 63)
                lineBuf[lineLen++] = (char)b;
        }
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
