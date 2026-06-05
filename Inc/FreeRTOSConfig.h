#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#include "stm32h7xx.h"

#ifdef __cplusplus
extern "C" {
#endif

void Error_Handler(void);

#define configENABLE_MPU 0U
#define configENABLE_FPU 1U
#define configENABLE_TRUSTZONE 0U

#define configUSE_PREEMPTION 1U
#define configUSE_TIME_SLICING 1U
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0U
#define configUSE_TICKLESS_IDLE 0U
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ ((TickType_t)1000U)
#define configMAX_PRIORITIES 7U
#define configMINIMAL_STACK_SIZE 256U
#define configUSE_16_BIT_TICKS 0U
#define configIDLE_SHOULD_YIELD 1U
#define configUSE_TASK_NOTIFICATIONS 1U
#define configQUEUE_REGISTRY_SIZE 0U
#define configMAX_TASK_NAME_LEN 16U
#define configUSE_MUTEXES 1U
#define configUSE_RECURSIVE_MUTEXES 0U
#define configUSE_COUNTING_SEMAPHORES 1U
#define configUSE_QUEUE_SETS 0U
#define configUSE_APPLICATION_TASK_TAG 0U
#define configUSE_NEWLIB_REENTRANT 0U
#define configUSE_TRACE_FACILITY 0U
#define configUSE_STATS_FORMATTING_FUNCTIONS 0U
#define configUSE_IDLE_HOOK 0U
#define configUSE_TICK_HOOK 0U
#define configCHECK_FOR_STACK_OVERFLOW 2U
#define configUSE_MALLOC_FAILED_HOOK 0U
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0U
#define configUSE_TIMERS 0U
#define configCHECK_HANDLER_INSTALLATION 0U

#define configSUPPORT_STATIC_ALLOCATION 0U
#define configSUPPORT_DYNAMIC_ALLOCATION 1U
#define configTOTAL_HEAP_SIZE ((size_t)(64U * 1024U))

#define INCLUDE_vTaskPrioritySet 1U
#define INCLUDE_vTaskDelete 1U
#define INCLUDE_vTaskDelay 1U
#define INCLUDE_vTaskSuspend 1U
#define INCLUDE_xTaskDelayUntil 1U
#define INCLUDE_xTaskGetSchedulerState 1U
#define INCLUDE_uxTaskGetStackHighWaterMark 1U

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4U
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U

#define configKERNEL_INTERRUPT_PRIORITY \
  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))

#define configASSERT(x)        \
  do {                         \
    if ((x) == 0) {           \
      __disable_irq();         \
      Error_Handler();         \
    }                          \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_CONFIG_H */
