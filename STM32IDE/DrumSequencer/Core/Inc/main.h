/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CLK_IN_Pin GPIO_PIN_0
#define CLK_IN_GPIO_Port GPIOA
#define CLK_IN_EXTI_IRQn EXTI0_IRQn
#define RE_CHAN_B_OUT_Pin GPIO_PIN_1
#define RE_CHAN_B_OUT_GPIO_Port GPIOA
#define SPI1_CS_MAX1_Pin GPIO_PIN_14
#define SPI1_CS_MAX1_GPIO_Port GPIOB
#define CLK_IN_DETECT_Pin GPIO_PIN_15
#define CLK_IN_DETECT_GPIO_Port GPIOB
#define RE_LEN_A_OUT_Pin GPIO_PIN_6
#define RE_LEN_A_OUT_GPIO_Port GPIOC
#define RE_LEN_B_OUT_Pin GPIO_PIN_7
#define RE_LEN_B_OUT_GPIO_Port GPIOC
#define MCP_INT_A_Pin GPIO_PIN_8
#define MCP_INT_A_GPIO_Port GPIOC
#define MCP_INT_A_EXTI_IRQn EXTI9_5_IRQn
#define MCP_INT_B_Pin GPIO_PIN_9
#define MCP_INT_B_GPIO_Port GPIOC
#define MCP_INT_B_EXTI_IRQn EXTI9_5_IRQn
#define RE_WIN_A_OUT_Pin GPIO_PIN_8
#define RE_WIN_A_OUT_GPIO_Port GPIOA
#define RE_WIN_B_OUT_Pin GPIO_PIN_9
#define RE_WIN_B_OUT_GPIO_Port GPIOA
#define RE_CHAN_A_OUT_Pin GPIO_PIN_15
#define RE_CHAN_A_OUT_GPIO_Port GPIOA
#define SPI2_CS_74HC595_Pin GPIO_PIN_4
#define SPI2_CS_74HC595_GPIO_Port GPIOB
#define SPI1_CS_MAX2_Pin GPIO_PIN_5
#define SPI1_CS_MAX2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
