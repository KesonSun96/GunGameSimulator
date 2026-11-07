#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_TINYUSB_NO_DEFAULT_TASK 1
#define CONFIG_TINYUSB_INIT_IN_DEFAULT_TASK 0
#define pdTRUE 1
#define pdPASS 1
#define configASSERT(x) assert(x)
typedef int BaseType_t;
typedef struct test_queue *QueueHandle_t;
typedef void *TaskHandle_t;
typedef enum { eSetBits } eNotifyAction;
typedef enum { HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT } hid_report_type_t;

QueueHandle_t xQueueCreate(unsigned depth, unsigned size);
int xQueueSend(QueueHandle_t q, const void *value, unsigned timeout);
int xQueueReceive(QueueHandle_t q, void *value, unsigned timeout);
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
                           void *arg, unsigned priority, TaskHandle_t *task, int core);
int xTaskNotifyWait(uint32_t clear_in, uint32_t clear_out, uint32_t *flags, unsigned timeout);
int xTaskNotify(TaskHandle_t task, uint32_t flags, eNotifyAction action);
int64_t esp_timer_get_time(void);
bool tud_disconnect(void);
bool tud_connect(void);
bool tud_hid_n_ready(uint8_t instance);
bool tud_hid_n_report(uint8_t instance, uint8_t id, const void *data, uint16_t len);
void tud_task_ext(uint32_t timeout, bool isr);
