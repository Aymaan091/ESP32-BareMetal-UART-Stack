#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "led_strip.h" // Includes the new dependency

#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE 1024
#define FRAME_START 0xAA
#define FRAME_END   0x55
#define RGB_LED_PIN 48 // ESP32-S3 standard RGB pin

static led_strip_handle_t led_strip;

// --- 1. RING BUFFER ---
typedef struct {
    uint8_t buffer[BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} ring_buffer_t;

ring_buffer_t rx_ring_buf = { .head = 0, .tail = 0 };

void rb_push(uint8_t data) {
    uint16_t next = (rx_ring_buf.head + 1) % BUF_SIZE;
    if (next != rx_ring_buf.tail) {
        rx_ring_buf.buffer[rx_ring_buf.head] = data;
        rx_ring_buf.head = next;
    }
}

int rb_pop(uint8_t *data) {
    if (rx_ring_buf.head == rx_ring_buf.tail) return 0;
    *data = rx_ring_buf.buffer[rx_ring_buf.tail];
    rx_ring_buf.tail = (rx_ring_buf.tail + 1) % BUF_SIZE;
    return 1;
}

// --- 2. CRC-8 CALCULATION ---
uint8_t calculate_crc8(uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc <<= 1;
        }
    }
    return crc;
}

// --- 3. HARDWARE INTERRUPT TASK ---
static QueueHandle_t uart_queue;
static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(BUF_SIZE);

    while (1) {
        if (xQueueReceive(uart_queue, (void *)&event, portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                int len = uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                for (int i = 0; i < len; i++) rb_push(dtmp[i]);
            } else {
                uart_flush_input(EX_UART_NUM);
                xQueueReset(uart_queue);
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

// --- 4. STATE MACHINE PARSER ---
typedef enum {
    STATE_WAIT_START, STATE_GET_CMD, STATE_GET_DATA, STATE_GET_CRC, STATE_WAIT_END
} parser_state_t;

static void parsing_task(void *pvParameters)
{
    uint8_t byte;
    parser_state_t current_state = STATE_WAIT_START;
    
    uint8_t current_cmd = 0, current_data = 0, current_crc = 0;

    while (1) {
        while (rb_pop(&byte)) {
            switch (current_state) {
                case STATE_WAIT_START:
                    if (byte == FRAME_START) current_state = STATE_GET_CMD;
                    break;
                case STATE_GET_CMD:
                    current_cmd = byte; current_state = STATE_GET_DATA;
                    break;
                case STATE_GET_DATA:
                    current_data = byte; current_state = STATE_GET_CRC;
                    break;
                case STATE_GET_CRC:
                    current_crc = byte; current_state = STATE_WAIT_END;
                    break;
                case STATE_WAIT_END:
                    if (byte == FRAME_END) { 
                        uint8_t payload[2] = {current_cmd, current_data};
                        if (calculate_crc8(payload, 2) == current_crc) {
                            
                            printf("\n[SUCCESS] Executing Cmd: 0x%02X | Data: 0x%02X\n", current_cmd, current_data);
                            
                            // --- HARDWARE EXECUTION ---
                            if (current_cmd == 0x01) { // 0x01 is our assigned LED Command
                                if (current_data == 0x01) {
                                    led_strip_set_pixel(led_strip, 0, 0, 0, 255); // Blue
                                } else if (current_data == 0x02) {
                                    led_strip_set_pixel(led_strip, 0, 0, 255, 0); // Green
                                } else if (current_data == 0x00) {
                                    led_strip_clear(led_strip);                   // Off
                                }
                                led_strip_refresh(led_strip); // Push the color to the hardware
                            }
                        }
                    }
                    current_state = STATE_WAIT_START;
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_main(void)
{
    // 1. Initialize RGB LED
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_PIN,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);

    // 2. Initialize UART
    uart_config_t uart_config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT
    };
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart_queue, 0);
    uart_param_config(EX_UART_NUM, &uart_config);
    uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // 3. Start Tasks
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);
    xTaskCreate(parsing_task, "parsing_task", 2048, NULL, 10, NULL);
    
    printf("\nLED Control Stack Running...\n");
}