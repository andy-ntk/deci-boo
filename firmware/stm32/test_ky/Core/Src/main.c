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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PWM_MAX              999U
/* บนไฟ 3.3V ขา R (Vf ~2.0V) สว่างกว่า G (Vf ~3.0V)
   จึงหรี่ R ลงตอนผสมสีเหลือง เพื่อไม่ให้เพี้ยนเป็นสีส้ม/แดง */
#define YELLOW_R_DUTY        250U
#define YELLOW_G_DUTY        999U
#define MIC_SAMPLE_WINDOW_MS 50U     /* เก็บ Peak-to-Peak ทุก 50 ms */
#define ALERT_BLINK_MS       250U    /* กระพริบแดงทุก 250 ms */
#define ALERT_HOLD_MS        1200U   /* ค้างสถานะเตือนเมื่อเสียงดังเกินเกณฑ์ */
#define WARN_HOLD_MS         600U    /* ค้างสถานะเริ่มดังกันไฟกระพริบถี่ */
#define DEFAULT_ALERT_TH     400     /* เกณฑ์เสียงดังเกินกำหนดเริ่มต้น (0-4095) */
#define MIN_ALERT_TH         100
#define MAX_ALERT_TH         3000
#define ENC_STEP_TH          25      /* หมุน KY-040 1 กิ๊ก ปรับเกณฑ์ทีละ 25 */
/* ตั้งเป็น 1 ถ้าปรับ pot สีฟ้าบน KY-037 แล้วและอยากให้ D0=0 ทริกเกอร์แดงด้วย */
#define USE_MIC_D0_TRIGGER   0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static uint8_t  u1_rx_byte = 0;
static char     u1_rx_buf[64];
static uint16_t u1_rx_idx = 0;
static volatile uint8_t u1_cmd_ready = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
typedef enum {
  FOCUS_QUIET = 0,
  FOCUS_WARN,
  FOCUS_ALERT
} FocusState_t;
static void RGB_Set(uint16_t r, uint16_t g, uint16_t b)
{
  if (r > PWM_MAX) r = PWM_MAX;
  if (g > PWM_MAX) g = PWM_MAX;
  if (b > PWM_MAX) b = PWM_MAX;
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, r); /* PB6: R */
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g); /* PB8: G */
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, b); /* PB9: B */
}
static uint16_t Mic_ReadPeakToPeak(uint32_t window_ms)
{
  uint32_t t_start = HAL_GetTick();
  uint16_t min_val = 4095;
  uint16_t max_val = 0;
  while ((HAL_GetTick() - t_start) < window_ms)
  {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 5) == HAL_OK)
    {
      uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
      if (val < min_val) min_val = val;
      if (val > max_val) max_val = val;
    }
    HAL_ADC_Stop(&hadc1);
  }
  return (max_val >= min_val) ? (max_val - min_val) : 0;
}
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
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
    RGB_Set(0, 0, 0);
    int32_t alert_threshold = DEFAULT_ALERT_TH;
    int32_t warn_threshold = alert_threshold / 2;
    uint16_t prev_enc_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
    int32_t enc_accum = 0;
    uint8_t prev_sw = GPIO_PIN_SET;
    uint32_t last_sw_ms = 0;
    uint32_t alert_until_ms = 0;
    uint32_t warn_until_ms = 0;
    uint32_t last_blink_ms = 0;
    uint8_t blink_on = 0;
    uint32_t last_log_ms = 0;
    FocusState_t state = FOCUS_QUIET;
    FocusState_t prev_state = FOCUS_QUIET;
    HAL_UART_Receive_IT(&huart1, &u1_rx_byte, 1);
      uint32_t last_esp_tx_ms = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  /* 1. อ่าน KY-040 หมุนปรับเกณฑ์เสียง */
	      uint16_t curr_enc_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);
	      int16_t enc_delta = (int16_t)(uint16_t)(curr_enc_cnt - prev_enc_cnt);
	      prev_enc_cnt = curr_enc_cnt;
	      if (enc_delta != 0)
	      {
	        enc_accum += enc_delta;
	        int32_t detents = enc_accum / 2; /* จาก test_ky_040: 1 กิ๊ก = 2 counts */
	        enc_accum %= 2;
	        if (detents != 0)
	        {
	          alert_threshold += detents * ENC_STEP_TH;
	          if (alert_threshold < MIN_ALERT_TH) alert_threshold = MIN_ALERT_TH;
	          if (alert_threshold > MAX_ALERT_TH) alert_threshold = MAX_ALERT_TH;
	          warn_threshold = alert_threshold / 2;
	        }
	      }
	      /* 2. อ่านปุ่ม SW (PA8, Active-Low) -> กดเพื่อรีเซ็ตเกณฑ์กลับค่าเริ่มต้น */
	      uint8_t curr_sw = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8);
	      uint32_t now = HAL_GetTick();
	      if (prev_sw == GPIO_PIN_SET && curr_sw == GPIO_PIN_RESET && (now - last_sw_ms) > 200U)
	      {
	        last_sw_ms = now;
	        alert_threshold = DEFAULT_ALERT_TH;
	        warn_threshold = alert_threshold / 2;
	        /* กระพริบสีน้ำเงินสั้นๆ ยืนยันการกดปุ่ม */
	        RGB_Set(0, 0, PWM_MAX);
	        HAL_Delay(120);
	        char reset_msg[64];
	        int n = snprintf(reset_msg, sizeof(reset_msg),
	                         "[KY-040 SW] Reset alert_th -> %ld\r\n", (long)alert_threshold);
	        HAL_UART_Transmit(&huart2, (uint8_t *)reset_msg, (uint16_t)n, 100);
	      }
	      prev_sw = curr_sw;
	      /* 3. อ่าน KY-037: A0 (Peak-to-Peak 50 ms) และ D0 (PA1) */
	      uint16_t amp = Mic_ReadPeakToPeak(MIC_SAMPLE_WINDOW_MS);
	      uint8_t d0 = (uint8_t)HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1);
	      now = HAL_GetTick();
	      /* 4. ตัดสินสถานะ Focus Mode พร้อม Hold Time */
	      uint8_t is_over_alert = (amp >= (uint16_t)alert_threshold)
	                              || (USE_MIC_D0_TRIGGER && d0 == GPIO_PIN_RESET);
	      uint8_t is_warn = (amp >= (uint16_t)warn_threshold);
	      if (is_over_alert)
	      {
	        alert_until_ms = now + ALERT_HOLD_MS;
	        warn_until_ms = now + WARN_HOLD_MS;
	      }
	      else if (is_warn)
	      {
	        warn_until_ms = now + WARN_HOLD_MS;
	      }
	      if ((int32_t)(alert_until_ms - now) > 0)
	      {
	        state = FOCUS_ALERT;
	      }
	      else if ((int32_t)(warn_until_ms - now) > 0)
	      {
	        state = FOCUS_WARN;
	      }
	      else
	      {
	        state = FOCUS_QUIET;
	      }
	      /* 5. ขับสี KY-009 RGB LED */
	      switch (state)
	      {
	        case FOCUS_QUIET:
	          blink_on = 0;
	          RGB_Set(0, PWM_MAX, 0); /* เขียว */
	          break;
	        case FOCUS_WARN:
	          blink_on = 0;
	          RGB_Set(YELLOW_R_DUTY, YELLOW_G_DUTY, 0); /* เหลือง */
	          break;
	        case FOCUS_ALERT:
	          if (prev_state != FOCUS_ALERT)
	          {
	            blink_on = 1;
	            last_blink_ms = now;
	          }
	          else if ((now - last_blink_ms) >= ALERT_BLINK_MS)
	          {
	            last_blink_ms = now;
	            blink_on = !blink_on;
	          }
	          RGB_Set(blink_on ? PWM_MAX : 0, 0, 0); /* แดงกระพริบเตือน */
	          break;
	      }
	      prev_state = state;
	      /* 6. แสดงผล Debug ผ่าน USART2 (115200 baud) ทุก 200 ms */
	      if ((now - last_log_ms) >= 200U)
	      {
	        last_log_ms = now;
	        const char *state_str = (state == FOCUS_QUIET) ? "QUIET(GREEN)" :
	                                (state == FOCUS_WARN)  ? "LOUD(YELLOW)" :
	                                                         "ALERT(RED_BLINK)";
	        char log_buf[128];
	        int len = snprintf(log_buf, sizeof(log_buf),
	                           "amp=%4u | d0=%u | warn_th=%ld | alert_th=%ld | state=%s\r\n",
	                           amp, d0, (long)warn_threshold, (long)alert_threshold, state_str);
	        HAL_UART_Transmit(&huart2, (uint8_t *)log_buf, (uint16_t)len, 100);
	      }
	      /* 7. รับคำสั่งจาก ESP32-S3 (USART1) */
	          if (u1_cmd_ready)
	          {
	            if (strcmp(u1_rx_buf, "PING") == 0)
	            {
	              const char *pong = "PONG\n";
	              HAL_UART_Transmit(&huart1, (uint8_t *)pong, (uint16_t)strlen(pong), 50);
	            }
	            else if (strncmp(u1_rx_buf, "SET_TH,", 7) == 0)
	            {
	              int32_t new_th = atoi(&u1_rx_buf[7]);
	              if (new_th < MIN_ALERT_TH) new_th = MIN_ALERT_TH;
	              if (new_th > MAX_ALERT_TH) new_th = MAX_ALERT_TH;
	              alert_threshold = new_th;
	              warn_threshold = alert_threshold / 2;
	              char ack[48];
	              int alen = snprintf(ack, sizeof(ack), "ACK,SET_TH,%ld\n", (long)alert_threshold);
	              HAL_UART_Transmit(&huart1, (uint8_t *)ack, (uint16_t)alen, 50);
	            }
	            u1_cmd_ready = 0;
	          }
	          /* 8. ส่งสถานะ Focus Mode ไป ESP32-S3 ทุก 500 ms */
	          if ((now - last_esp_tx_ms) >= 500U)
	          {
	            last_esp_tx_ms = now;
	            char esp_buf[64];
	            int elen = snprintf(esp_buf, sizeof(esp_buf),
	                                "STAT,%u,%ld,%ld,%d\n",
	                                amp, (long)warn_threshold, (long)alert_threshold, (int)state);
	            HAL_UART_Transmit(&huart1, (uint8_t *)esp_buf, (uint16_t)elen, 50);
	          }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

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
  sConfig.IC1Filter = 15;
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

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 83;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    if (!u1_cmd_ready)
    {
      if (u1_rx_byte == '\n')
      {
        u1_rx_buf[u1_rx_idx] = '\0';
        u1_rx_idx = 0;
        u1_cmd_ready = 1;
      }
      else if (u1_rx_byte != '\r')
      {
        if (u1_rx_idx < (sizeof(u1_rx_buf) - 1U))
        {
          u1_rx_buf[u1_rx_idx++] = (char)u1_rx_byte;
        }
        else
        {
          u1_rx_idx = 0;
        }
      }
    }
    HAL_UART_Receive_IT(&huart1, &u1_rx_byte, 1);
  }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    u1_rx_idx = 0;
    HAL_UART_Receive_IT(&huart1, &u1_rx_byte, 1);
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
