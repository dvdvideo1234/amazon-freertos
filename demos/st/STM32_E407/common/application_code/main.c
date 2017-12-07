/*
 * Amazon FreeRTOS V1.0.0
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software. If you wish to use our Amazon
 * FreeRTOS name, please do so in a fair use way that does not cause confusion.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/*
 * Debug setup instructions:
 * 1) Open the debug configuration dialog.
 * 2) Go to the Debugger tab.
 * 3) If the 'Mode Setup' options are not visible, click the 'Show Generator' button.
 * 4) In the Mode Setup|Reset Mode drop down ensure that
 *    'Software System Reset' is selected.
 */

#include "main.h"
#include "stdint.h"
#include "stdarg.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "FreeRTOS_sockets.h"
#include "FreeRTOS_ip.h"
#include "task.h"

/* Board includes */
#include "gpio.h"
#include "rng.h"
#include "rtc.h"
#include "usart.h"
#include "ethernetif.h"

/* Demo includes */
#include "aws_demo_runner.h"
#include "aws_system_init.h"
#include "aws_logging_task.h"
#include "aws_clientcredential.h"
#include "aws_dev_mode_key_provisioning.h"

#define mainLOGGING_TASK_STACK_SIZE         ( configMINIMAL_STACK_SIZE * 5 )
#define mainLOGGING_MESSAGE_QUEUE_LENGTH    ( 15 )

void vApplicationDaemonTaskStartupHook(void);

/**********************
 * Global Variables
 **********************/
RTC_HandleTypeDef xHrtc;
RNG_HandleTypeDef xHrng;

/* The MAC address array is not declared const as the MAC address will
 normally be read from an EEPROM and not hard coded (in real deployed
 applications).*/
uint8_t ucMACAddress[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };

/* Define the network addressing.  These parameters will be used if either
 ipconfigUDE_DHCP is 0 or if ipconfigUSE_DHCP is 1 but DHCP auto configuration
 failed. */
const uint8_t ucIPAddress[4] = { 192, 168, 20, 1 };
const uint8_t ucNetMask[4] = { 255, 255, 255, 0 };
const uint8_t ucGatewayAddress[4] = { 192, 168, 20, 254 };

/* The following is the address of an OpenDNS server. */
const uint8_t ucDNSServerAddress[4] = { 208, 67, 222, 222 };

static const char* xHostName = "roedan";

/* Private variables ---------------------------------------------------------*/
static UART_HandleTypeDef xConsoleUart;
static BaseType_t xLinkUp = pdFALSE;
static TaskHandle_t xLedTaskHandle;

/* Private function prototypes -----------------------------------------------*/
static void SystemClock_Config(void);
static void prvLedFlasherTask(void* argument);

/**
 * @brief Initializes the STM32L475 IoT node board.
 *
 * Initialization of clock, LEDs, RNG, RTC, and WIFI module.
 */
static void prvMiscInitialization(void);

/**
 * @brief Application runtime entry point.
 */
int main(void)
{
    /* Perform any hardware initialization that does not require the RTOS to be
     * running.  */
    prvMiscInitialization();

    /* Create tasks that are not dependent on the network stack */
    xLoggingTaskInitialize( mainLOGGING_TASK_STACK_SIZE,
    configMAX_PRIORITIES - 1, /* Not ideal but allows logging during TLS negotiation. */
    mainLOGGING_MESSAGE_QUEUE_LENGTH);

    /* Initialise the RTOS's TCP/IP stack.  The tasks that use the network
     are created in the vApplicationIPNetworkEventHook() hook function
     below.  The hook function is called when the network connects. */
    FreeRTOS_IPInit(ucIPAddress, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress);

    /* Start the scheduler.  Initialization that requires the OS to be running,
     * including the WiFi initialization, is performed in the RTOS daemon task
     * startup hook. */
    vTaskStartScheduler();

    return 0;
}
/*-----------------------------------------------------------*/

void vApplicationDaemonTaskStartupHook(void)
{
    /* start the LED flasher task */
    xTaskCreate(prvLedFlasherTask, "LED", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+1, &xLedTaskHandle);

    /* A simple example to demonstrate key and certificate provisioning in
     * microcontroller flash using PKCS#11 interface. This should be replaced
     * by production ready key provisioning mechanism. */
    vDevModeKeyProvisioning();
}

/* Defined by the application code, but called by FreeRTOS+UDP when the network
 connects/disconnects (if ipconfigUSE_NETWORK_EVENT_HOOK is set to 1 in
 FreeRTOSIPConfig.h). */
void vApplicationIPNetworkEventHook(eIPCallbackEvent_t eNetworkEvent)
{
    uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
    static BaseType_t xTasksAlreadyCreated = pdFALSE;
    int8_t cBuffer[16];

    /* Check this was a network up event, as opposed to a network down event. */
    if (eNetworkEvent == eNetworkUp)
    {
        /* Create the tasks that use the IP stack if they have not already been
         created. */
        if (xTasksAlreadyCreated == pdFALSE)
        {
            /*
             * Create the tasks here.
             */

            xTasksAlreadyCreated = pdTRUE;
        }

        /* The network is up and configured.  Print out the configuration,
         which may have been obtained from a DHCP server. */
        FreeRTOS_GetAddressConfiguration(&ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress);

        /* Convert the IP address to a string then print it out. */
        FreeRTOS_inet_ntoa(ulIPAddress, cBuffer);
        configPRINTF(("IP Address: %s\r\n", cBuffer));

        /* Convert the net mask to a string then print it out. */
        FreeRTOS_inet_ntoa(ulNetMask, cBuffer);
        configPRINTF(("Subnet Mask: %s\r\n", cBuffer));

        /* Convert the IP address of the gateway to a string then print it out. */
        FreeRTOS_inet_ntoa(ulGatewayAddress, cBuffer);
        configPRINTF(("Gateway IP Address: %s\r\n", cBuffer));

        /* Convert the IP address of the DNS server to a string then print it out. */
        FreeRTOS_inet_ntoa(ulDNSServerAddress, cBuffer);
        configPRINTF(("DNS server IP Address: %s\r\n", cBuffer ));
        xLinkUp = pdTRUE;
        if (SYSTEM_Init() == pdPASS)
        {
            DEMO_RUNNER_RunDemos();
        }
    }
    else
    {
        configPRINTF(("Link down :(\r\n"));
        xLinkUp = pdFALSE;
    }
}

/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetIdleTaskMemory() to provide the memory that is
 * used by the Idle task. */
void vApplicationGetIdleTaskMemory(StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t * pulIdleTaskStackSize)
{
    /* If the buffers to be provided to the Idle task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* Pass out the array that will be used as the Idle task's stack. */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/*-----------------------------------------------------------*/

/* configUSE_STATIC_ALLOCATION is set to 1, so the application must provide an
 * implementation of vApplicationGetTimerTaskMemory() to provide the memory that is
 * used by the RTOS daemon/time task. */
void vApplicationGetTimerTaskMemory(StaticTask_t ** ppxTimerTaskTCBBuffer, StackType_t ** ppxTimerTaskStackBuffer, uint32_t * pulTimerTaskStackSize)
{
    /* If the buffers to be provided to the Timer task are declared inside this
     * function then they must be declared static - otherwise they will be allocated on
     * the stack and so not exists after this function exits. */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    /* Pass out a pointer to the StaticTask_t structure in which the Idle
     * task's state will be stored. */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* Pass out the array that will be used as the Timer task's stack. */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* Pass out the size of the array pointed to by *ppxTimerTaskStackBuffer.
     * Note that, as the array is necessarily of type StackType_t,
     * configMINIMAL_STACK_SIZE is specified in words, not bytes. */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
/*-----------------------------------------------------------*/

/**
 * @brief Publishes a character to the STM32L475 UART
 *
 * This is used to implement the tinyconfigPRINTF created by Spare Time Labs
 * http://www.sparetimelabs.com/tinyconfigPRINTF/tinyconfigPRINTF.php
 *
 * @param pv    unused void pointer for compliance with tinyconfigPRINTF
 * @param ch    character to be printed
 */
void vSTM32F407putc(void * pv, char ch)
{
    while (HAL_OK != HAL_UART_Transmit(&xConsoleUart, (uint8_t *)&ch, 1, 30000))
    {
    }
}
/*-----------------------------------------------------------*/

/**
 * @brief Initializes the board.
 */
static void prvMiscInitialization(void)
{
    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock. */
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_USART6_UART_Init();
    MX_RTC_Init();
    MX_RNG_Init();

}

/* StartDefaultTask function */
static void prvLedFlasherTask(void* argument)
{
    /* USER CODE BEGIN StartDefaultTask */
    /* Infinite loop */
    for (;;)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        vTaskDelay(500);
    }
    /* USER CODE END StartDefaultTask */
}

/*-----------------------------------------------------------*/

/**
 * @brief Initializes the system clock.
 */
static void SystemClock_Config(void)
{

    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;

    /**Configure the main internal regulator output voltage
     */
    __HAL_RCC_PWR_CLK_ENABLE()
    ;

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /**Initializes the CPU, AHB and APB busses clocks
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 12;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    /**Initializes the CPU, AHB and APB busses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    /**Configure the Systick interrupt time
     */
    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);

    /**Configure the Systick
     */
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

    /* SysTick_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(SysTick_IRQn, 15, 0);
}
/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

/**
 * @brief Warn user if pvPortMalloc fails.
 *
 * Called if a call to pvPortMalloc() fails because there is insufficient
 * free memory available in the FreeRTOS heap.  pvPortMalloc() is called
 * internally by FreeRTOS API functions that create tasks, queues, software
 * timers, and semaphores.  The size of the FreeRTOS heap is set by the
 * configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h.
 *
 */
void vApplicationMallocFailedHook()
{
    configPRINTF(( "ERROR: Malloc failed to allocate memory\r\n" ));
}
/*-----------------------------------------------------------*/

/**
 * @brief Loop forever if stack overflow is detected.
 *
 * If configCHECK_FOR_STACK_OVERFLOW is set to 1,
 * this hook provides a location for applications to
 * define a response to a stack overflow.
 *
 * Use this hook to help identify that a stack overflow
 * has occurred.
 *
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char * pcTaskName)
{
    portDISABLE_INTERRUPTS();

    /* Loop forever */
    for (;;)
        ;
}

/*-----------------------------------------------------------*/

void vApplicationIdleHook(void)
{
}
/*-----------------------------------------------------------*/

BaseType_t xApplicationDNSQueryHook(const char *pcName)
{
    BaseType_t retVal = pdFALSE;
    if (strcmp(xHostName, pcName) == 0)
    {
        retVal = pdTRUE;
    }
    return retVal;
}

/*-----------------------------------------------------------*/

void vOutputChar(const char cChar, const TickType_t xTicksToWait)
{
    (void)cChar;
    (void)xTicksToWait;
}
/*-----------------------------------------------------------*/

/**
 * @brief  This function is executed in case of error occurrence.
 * @param  None
 * @retval None
 */
void _Error_Handler(char * file, int line)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT

/**
 * @brief Reports the name of the source file and the source line number
 * where the assert_param error has occurred.
 * @param file: pointer to the source file name
 * @param line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
     ex: configPRINTF("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */

}

#endif

/*-----------------------------------------------------------*/
