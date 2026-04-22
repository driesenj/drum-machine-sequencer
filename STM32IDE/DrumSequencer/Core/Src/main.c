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
#include "stdio.h"
#include "MAX7219.h"
#include "MCP23017.h"
#include "74HC595.h"
#include "Encoder.h"
#include "SSD1306.h"
#include "sequencer.h"
#include "ui.h"
#include "utils.h"
#include "flash.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEBUG_LOG 1

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

// ── External Clock Input  ──────────────────────────────────────────────────────────────
volatile uint8_t clock_tick = 0;  // set by ISR, consumed by main loop

// ── UI ─────────────────────────────────────────────────────────────
UIContext_t ui_ctx;

// ── Encoders ──────────────────────────────────────────────────────────────
// A = channel select  → TIM2
// B = channel length  → TIM3
// C = view window     → TIM1
Encoder_t *encA;
Encoder_t *encB;
Encoder_t *encC;

// Last known counts — used to compute delta each loop tick
static int16_t last_cnt_A = 0;
static int16_t last_cnt_B = 0;
static int16_t last_cnt_C = 0;

// ── Sequencer ─────────────────────────────────────────────────────────────
Sequencer_t seq;

// ── MCP ───────────────────────────────────────────────────────────────────
MCP23017_Handle mcp;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_SPI2_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void apply_clock_mode_impl(uint8_t clock_mode) {
    GPIO_InitTypeDef g = {
        .Pin   = CLK_IN_Pin,
        .Pull  = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    if (clock_mode == 1) {
        g.Mode = GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_Init(CLK_IN_GPIO_Port, &g);
        HAL_NVIC_DisableIRQ(EXTI0_IRQn);
    } else {
        g.Mode = GPIO_MODE_IT_RISING;
        HAL_GPIO_Init(CLK_IN_GPIO_Port, &g);
        HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    }
}

static void clock_output(uint8_t high) {
	HAL_GPIO_WritePin(CLK_IN_GPIO_Port, CLK_IN_Pin, high);
}

static UIFlashResult_t flash_write_impl(uint8_t slot, const SeqPreset_t *preset) {
    return Flash_WritePreset(slot, preset) == HAL_OK ? UI_FLASH_OK : UI_FLASH_ERR;
}
static void flash_read_impl(uint8_t slot, SeqPreset_t *preset) {
    Flash_ReadPreset(slot, preset);
}
static uint8_t flash_valid_impl(uint8_t slot) {
    return Flash_SlotValid(slot);
}

// ── MCP23017 button callback ───────────────────────────────────────────────
static void OnStepButton(MCP23017_Port port, MCP23017_Pin pin, uint8_t state) {
	if (port != MCP23017_PORT_A || pin >= 8) return;

	uint32_t now = HAL_GetTick();

	UIQueue_Push((UIEvent_t){
		.tick_ms = now,
		.type  = state ? EVT_STEP_PRESS : EVT_STEP_RELEASE,
		.value = pin
	});
}

static void OnFunctionButton(MCP23017_Port port, MCP23017_Pin pin, uint8_t state) {
	if (port != MCP23017_PORT_B) return;

	uint32_t now = HAL_GetTick();

	switch (pin) {
	case 0:
		UIQueue_Push((UIEvent_t){
			.tick_ms = now,
			.type = state ? EVT_ALT_PRESS : EVT_ALT_RELEASE
		});
		break;
	case 1:
		UIQueue_Push((UIEvent_t){
			.tick_ms = now,
					.type = state ? EVT_FILL_PRESS : EVT_FILL_RELEASE
		});
		break;
	case 5:
		UIQueue_Push((UIEvent_t){
			.tick_ms = now,
					.type = state ? EVT_RECORD_PRESS : EVT_RECORD_RELEASE
		});
		break;
	case 2: if (state == 1) UIQueue_Push((UIEvent_t){ .tick_ms = now, .type = EVT_ENC_B_PUSH }); break;
	case 3: if (state == 1) UIQueue_Push((UIEvent_t){ .tick_ms = now,.type = EVT_ENC_A_PUSH }); break;
	case 4: if (state == 1) UIQueue_Push((UIEvent_t){ .tick_ms = now,.type = EVT_ENC_C_PUSH }); break;
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
  MX_SPI1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_SPI2_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

	 printf("\n\n\n\n-----------------\r\nStarting\r\n-----------------\r\n");

	 // ── Sequencer ─────────────────────────────────────────────────────────
	 Seq_Init(&seq, clock_output);
	 printf("Initialized Sequencer\r\n");

	 // ── UI ─────────────────────────────────────────────────────────
	 UI_Init(&ui_ctx,
	         flash_write_impl,
	         flash_read_impl,
	         flash_valid_impl,
	         apply_clock_mode_impl);
	 printf("Initialized UI\r\n");

	 // ── Encoders ──────────────────────────────────────────────────────────
	 encA = Encoder_Add(&htim3); // Turn A = channel select
	 encB = Encoder_Add(&htim2); // Turn B = channel length
	 encC = Encoder_Add(&htim1); // Turn C = view window

	 Encoder_Start(encA);
	 Encoder_Start(encB);
	 Encoder_Start(encC);

	 last_cnt_A = Encoder_GetCount(encA);
	 last_cnt_B = Encoder_GetCount(encB);
	 last_cnt_C = Encoder_GetCount(encC);

	 printf("Initialized Encoders\r\n");

	 MAX7219_init();

	 printf("Initialized MAX7219\r\n");
	 //enc1 = Encoder_Add(&htim3);
	 //Encoder_Start(enc1);

	 MCP23017_InitHandle(&mcp, &hi2c1, 0);

	 // Step buttons → PORT_A pins 0–7
	 for (uint8_t i = 0; i < 8; i++) {
		 MCP23017_SetPinMode(&mcp,     MCP23017_PORT_A, i, MCP23017_MODE_INPUT);
		 MCP23017_SetPinCallback(&mcp, MCP23017_PORT_A, i, OnStepButton);
	 }

	 // Encoder A push button
	 MCP23017_SetPinMode(&mcp,     MCP23017_PORT_B, 3, MCP23017_MODE_INPUT);
	 MCP23017_SetPinCallback(&mcp, MCP23017_PORT_B, 3, OnFunctionButton);

	 // Encoder B push button
	 MCP23017_SetPinMode(&mcp,     MCP23017_PORT_B, 2, MCP23017_MODE_INPUT);
	 MCP23017_SetPinCallback(&mcp, MCP23017_PORT_B, 2, OnFunctionButton);

	 // Encoder C push button
	 MCP23017_SetPinMode(&mcp,     MCP23017_PORT_B, 4, MCP23017_MODE_INPUT);
	 MCP23017_SetPinCallback(&mcp, MCP23017_PORT_B, 4, OnFunctionButton);

	 // ALT Button
	 MCP23017_SetPinMode(&mcp,     MCP23017_PORT_B, 0, MCP23017_MODE_INPUT);
	 MCP23017_SetPinCallback(&mcp, MCP23017_PORT_B, 0, OnFunctionButton);

	 // FILL Button
	 MCP23017_SetPinMode(&mcp,     MCP23017_PORT_B, 1, MCP23017_MODE_INPUT);
	 MCP23017_SetPinCallback(&mcp, MCP23017_PORT_B, 1, OnFunctionButton);

	 // Record Button
	 MCP23017_SetPinMode(&mcp,     MCP23017_PORT_B, 5, MCP23017_MODE_INPUT);
	 MCP23017_SetPinCallback(&mcp, MCP23017_PORT_B, 5, OnFunctionButton);

	 printf("Initialized MCP23017\r\n");

	 HC595_init(&hspi2);
	 HC595_clear();

	 // Test: Set all button LEDs on
	 uint8_t data[HC595_NUM_DEVICES];
	 data[0] = 0xFF;
	 data[1] = 0x00;
	 data[2] = 	0x00;

	 HC595_write(data);

	 printf("Initialized 74HC595\r\n");

	 SSD1306_Init(&hi2c1);

	 // Splash screen
	 SSD1306_Fill(0);
	 printf("Initialized SSD1306\r\n");

	 seq.running = 1;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	 uint32_t prev = HAL_GetTick();
	 seq.last_clk_tick = prev;
	 while (1)
	 {
		 uint32_t now = HAL_GetTick();

		 // ── Clock ─────────────────────────────────────────────────────────
		 uint8_t clock_fired = 0;
		 if (seq.clock_mode == 1) {
			 clock_fired = Seq_TickClock(&seq, now);
		 } else if (clock_tick) {
			 clock_tick  = 0;
			 clock_fired = 1;
		 }

		 // ── Sequencer advance ─────────────────────────────────────────────
		 uint16_t ticked_mask = Seq_AdvanceAll(&seq, now, clock_fired);

		 if (ticked_mask) {
		     if (ui_ctx.fill_held && (ticked_mask & (1u << seq.current_channel)))
		         seq.channels[seq.current_channel].gate_forced = 1;
		     ui_ctx.matrix_dirty = 1;
		 }


		 // ── Roll + gate outputs ───────────────────────────────────────────
		 Seq_UpdateRoll(&seq, now);
		 Seq_UpdateGateOutputs(&seq, now);

		 // ── Input ─────────────────────────────────────────────────────────
		 if (Encoder_HasChanged(encA)) {
			 int16_t cnt = Encoder_GetCount(encA);
			 UIQueue_Push((UIEvent_t){ .tick_ms = now, .type = EVT_ENC_A_TURN, .value = cnt - last_cnt_A });
			 last_cnt_A = cnt;
		 }

		 // ── Encoder B ─────────────────────────────────────────────────────────
		 if (Encoder_HasChanged(encB)) {
			 int16_t cnt = Encoder_GetCount(encB);
			 UIQueue_Push((UIEvent_t){ .tick_ms = now, .type = EVT_ENC_B_TURN, .value = cnt - last_cnt_B });
			 last_cnt_B = cnt;
		 }

		 // ── Encoder C ─────────────────────────────────────────────────────────
		 if (Encoder_HasChanged(encC)) {
			 int16_t cnt = Encoder_GetCount(encC);
			 UIQueue_Push((UIEvent_t){ .tick_ms = now, .type = EVT_ENC_C_TURN, .value = cnt - last_cnt_C });
			 last_cnt_C = cnt;
		 }

		 MCP23017_Poll(&mcp);

		 UIEvent_t evt;
		 while (UIQueue_Pop(&evt)) {
			 UI_Dispatch(&ui_ctx, &seq, &evt);
		 }

		 // ── Display ───────────────────────────────────────────────────────
		 static uint32_t last_ui_update_ms = 0;

		 if (ui_ctx.stepleds_dirty) {
			 UI_UpdateStepLEDs(&ui_ctx, &seq);
			 ui_ctx.stepleds_dirty = 0;
		 }

		 // Only update the display if more than 40ms remaining until next clock tick
		 // Display update takes around 31ms
		 uint32_t next_tick_ms = Seq_NextTickMs(&seq, now);

		 if (ui_ctx.display_dirty && (next_tick_ms > now) && (next_tick_ms - now) >= 40) {
			 UI_UpdateDisplay(&ui_ctx, &seq);
			 ui_ctx.display_dirty = 0;
			 last_ui_update_ms = now;
		 }

		 if (prev < ui_ctx.channel_flash_until_ms && now > ui_ctx.channel_flash_until_ms) {
			 ui_ctx.matrix_dirty = 1;
			 ui_ctx.channel_flash_until_ms = 0;
		 }
		 if (prev < ui_ctx.length_flash_until_ms && now > ui_ctx.length_flash_until_ms) {
			 ui_ctx.matrix_dirty = 1;
			 ui_ctx.length_flash_until_ms = 0;
		 }

		 if (ui_ctx.matrix_dirty && (now - last_ui_update_ms) >= 10) {
			 UI_UpdateMatrix(&ui_ctx, &seq);
			 ui_ctx.matrix_dirty = 0;
			 last_ui_update_ms = now;
		 }

		 prev = now;
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00F12981;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

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
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
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
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

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
  sConfig.EncoderMode = TIM_ENCODERMODE_TI1;
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
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
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

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI1;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

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
  sConfig.EncoderMode = TIM_ENCODERMODE_TI1;
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
  htim6.Init.Prescaler = 0;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 65535;
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
  huart2.Init.WordLength = UART_WORDLENGTH_7B;
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
  HAL_GPIO_WritePin(GPIOB, SPI1_CS_MAX1_Pin|SPI1_CS_MAX2_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, CLK_IN_DETECT_Pin|SPI2_CS_74HC595_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : CLK_IN_Pin */
  GPIO_InitStruct.Pin = CLK_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SPI1_CS_MAX1_Pin CLK_IN_DETECT_Pin SPI2_CS_74HC595_Pin SPI1_CS_MAX2_Pin */
  GPIO_InitStruct.Pin = SPI1_CS_MAX1_Pin|CLK_IN_DETECT_Pin|SPI2_CS_74HC595_Pin|SPI1_CS_MAX2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : MCP_INT_A_Pin */
  GPIO_InitStruct.Pin = MCP_INT_A_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MCP_INT_A_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MCP_INT_B_Pin */
  GPIO_InitStruct.Pin = MCP_INT_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MCP_INT_B_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

 /**
  * @brief  Retargets the C library printf function to the USART.
  *   None
  * @retval None
  */
 PUTCHAR_PROTOTYPE
 {
#if DEBUG_LOG
	 /* Place your implementation of fputc here */
	 /* e.g. write a character to the USART1 and Loop until the end of transmission */
	 HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 0xFFFF);
#endif
	 return ch;
 }

 void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
 {
	 if (GPIO_Pin == CLK_IN_Pin) {
		 clock_tick = 1;
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
