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
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_rx;

TIM_HandleTypeDef htim16;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM16_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define BUF_SIZE 220
#define OUTLIER_THRESHOLD 400

uint16_t RX_Buffer[BUF_SIZE];
// Moving average filter buffer, stores the filtered output before transmitting
uint16_t filtered_Buffer[BUF_SIZE];
volatile uint8_t data_ready = 2048;

// Keeps track of the last sample from the previous buffer
uint16_t prev_sample = 0;
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{

	 if (hspi->Instance == SPI1)
	{
		//HAL_GPIO_TogglePin(Test_GPIO_Port, Test_Pin);
		data_ready = 1;
	}
}

typedef enum {
    SENSOR_TRIGGER,
    SENSOR_WAIT_RISING,
    SENSOR_WAIT_FALLING,
	SENSOR_IDLE
} SensorState;

SensorState sensor_state = SENSOR_TRIGGER;
#define TIMEOUT_US 30000
#define SENSOR_OUTLIERS 3
#define CLOSE_THRESHOLD_CM 10
#define STOP_DELAY_MS 1000
uint16_t consecutive_data = 0;
uint32_t echo_time = 0;
volatile uint8_t recording = 0;
uint32_t sensor_timestamp = 0;
uint32_t distance_cm = 0;
static uint8_t close_count = 0;
static uint32_t last_seen_time = 0;


void sensor_update(void)
{

	switch(sensor_state)
	{
		case SENSOR_TRIGGER:
			HAL_GPIO_WritePin(trigger_GPIO_Port, trigger_Pin, GPIO_PIN_SET);
			__HAL_TIM_SET_COUNTER(&htim16, 0);
			sensor_state = SENSOR_WAIT_RISING;
			break;

		case SENSOR_WAIT_RISING:

			if (__HAL_TIM_GET_COUNTER(&htim16) >= 10)
				HAL_GPIO_WritePin(trigger_GPIO_Port, trigger_Pin, GPIO_PIN_RESET);

			// echo HIGH
			if (HAL_GPIO_ReadPin(echo_GPIO_Port, echo_Pin))
			{
				__HAL_TIM_SET_COUNTER(&htim16, 0);
				sensor_state = SENSOR_WAIT_FALLING;
			}

			// timeout: no echo at all
			else if (__HAL_TIM_GET_COUNTER(&htim16) > TIMEOUT_US)
			{
				sensor_timestamp = HAL_GetTick();
				sensor_state = SENSOR_IDLE;
			}

			break;

		case SENSOR_WAIT_FALLING:

			// echo LOW (normal completion)
			if (!HAL_GPIO_ReadPin(echo_GPIO_Port, echo_Pin))
			{
				echo_time = __HAL_TIM_GET_COUNTER(&htim16);
				distance_cm = echo_time / 58;
//				char buf[32];
//				sprintf(buf, "dist: %lu\r\n", distance_cm);
//				HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 100);
				if (distance_cm > 0 && distance_cm < CLOSE_THRESHOLD_CM)
				{

				    if (close_count < SENSOR_OUTLIERS)
				        close_count++;
				}
				else
				{
				    close_count = 0;
				}

				if (close_count >= SENSOR_OUTLIERS)
				{
				    recording = 1;
				    last_seen_time = HAL_GetTick();
				}

				// stop condition
				if (recording == 1)
				{

				    if (HAL_GetTick() - last_seen_time > STOP_DELAY_MS)
				    {

				        recording = 0;
				        close_count = 0;
				    }
				}

				sensor_timestamp = HAL_GetTick();
				sensor_state = SENSOR_IDLE;
			}

			// timeout: echo stuck HIGH or glitch
			else if (__HAL_TIM_GET_COUNTER(&htim16) > TIMEOUT_US)
			{
				sensor_timestamp = HAL_GetTick();
				sensor_state = SENSOR_IDLE;
			}

			break;

		case SENSOR_IDLE:

			if (HAL_GetTick() - sensor_timestamp >= 60)
				sensor_state = SENSOR_TRIGGER;

			break;
	}
}

void upload_data()
{
	data_ready = 0;

	  /* outlier rejection */
	  uint32_t sum = 0;
	  for (int i = 0; i < BUF_SIZE; i++)
		  sum += RX_Buffer[i] & 0x0FFF;
	  uint16_t mean = (uint16_t)(sum / BUF_SIZE);

	  for (int i = 0; i < BUF_SIZE; i++)
	  {
		  uint16_t sample = RX_Buffer[i] & 0x0FFF;
		  int32_t diff = (int32_t)sample - (int32_t)mean;
		  if (diff < 0) diff = -diff;
		  if (diff > OUTLIER_THRESHOLD)
			  RX_Buffer[i] = (i == 0) ? prev_sample : RX_Buffer[i - 1] & 0x0FFF;
		  else
			  RX_Buffer[i] = sample;
	  }


	  for (int i = 0; i < BUF_SIZE; i++)
	  {
		  uint16_t cur  = RX_Buffer[i] & 0x0FFF;
		  uint16_t prev = (i == 0) ? (prev_sample & 0x0FFF) : (RX_Buffer[i-1] & 0x0FFF);
		  filtered_Buffer[i] = (cur + prev) >> 1;  // divide by 2 with shift
	  }

	  prev_sample = RX_Buffer[BUF_SIZE - 1];  // save for next buffer boundary

	  HAL_GPIO_TogglePin(Test_GPIO_Port, Test_Pin);

	  /* transmit header then filtered audio data */
	  uint8_t header[4] = {0xAA, 0xBB, 0xCC, 0xDD};
	  HAL_UART_Transmit(&huart2, header, 4, 10);
	  HAL_UART_Transmit_DMA(&huart2, (uint8_t*)filtered_Buffer, BUF_SIZE * 2);
	  // re-arm
	  HAL_SPI_Receive_DMA(&hspi1, (uint8_t*)RX_Buffer, BUF_SIZE);
}

uint8_t uart_rx_byte = 0;

// 3 is manual, 4 is distance trigger
volatile uint8_t mode = 3;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        if (uart_rx_byte == 3)
            mode = 3;
        else if (uart_rx_byte == 4)
            mode = 4;

        // re-arm
        HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
    }
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
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  MX_TIM16_Init();
  /* USER CODE BEGIN 2 */
  HAL_SPI_Receive_DMA(&hspi1, (uint8_t*)RX_Buffer, BUF_SIZE);
  HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1);
  HAL_TIM_Base_Start(&htim16);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  sensor_update();

	  if (data_ready)
	  {
		  if (mode == 3) // if state is in manual mode, constantly upload
		  {
			  upload_data();
		  }
		  else if (mode == 4) // if state is in distance trigger mode
		  {

			  if (recording == 1)
			  {
				  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 1);

				  upload_data();
			  }
			  else {
				  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 0);
			  }
		  }


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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_16BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */
  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 31;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 65535;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */

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
  huart2.Init.BaudRate = 921600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
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
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(Test_GPIO_Port, Test_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD3_Pin|trigger_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : echo_Pin */
  GPIO_InitStruct.Pin = echo_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(echo_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : Test_Pin */
  GPIO_InitStruct.Pin = Test_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(Test_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD3_Pin trigger_Pin */
  GPIO_InitStruct.Pin = LD3_Pin|trigger_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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

#ifdef  USE_FULL_ASSERT
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
