/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "i2c_oled.h"
#include "cbspI2C.h"
#include "cBMP280.h"
#include "cSMP3011.h"

static const char *TAG = "example";

/*
        HARDWARE DEFINITIONS
*/
#define EXAMPLE_PIN_BUTTON GPIO_NUM_17
#define I2C_BUS_PORT                  0
#define EXAMPLE_PIN_NUM_SDA           GPIO_NUM_5
#define EXAMPLE_PIN_NUM_SCL           GPIO_NUM_4
#define EXAMPLE_PIN_LED               GPIO_NUM_16               

/*
        Components
*/
cbspI2C I2CChannel1;
cBMP280 BMP280;
cSMP3011 SMP3011;


/*
        TASKS
*/
//Prototypes
void TaskBlink(void *parameter);
void TaskDisplay(void *parameter);
void TaskSensors(void *parameter);

//Handlers
TaskHandle_t taskBlinkHandle   = nullptr;
TaskHandle_t taskDisplayHandle = nullptr;
TaskHandle_t taskSensorsHandle = nullptr;

int nova = 0;
int valor = 0;
bool useCelsius = true;
float convertToFahrenheit(float celsius) {
    return (celsius * 9.0 / 5.0) + 32;
}

int currentScreen = 0;  // Controle da tela (0 = Tela de pressão, 1 = Tela de temperatura, 2 = Outra tela)
bool usePSI = false;    // Se verdadeiro, exibe pressão em PSI, caso contrário em Bar

// Função de conversão de Pa para PSI
float convertToPSI(float pressurePa) {
    return pressurePa * 0.0001450377; // 1 Pascal = 0.0001450377 PSI
}

// Função de conversão de Pa para Bar
float convertToBar(float pressurePa) {
    return pressurePa * 0.00001; // 1 Pascal = 0.00001 Bar
}


//Main function
extern "C"
void app_main()
{
    // Setup pin for LED
    gpio_set_direction(EXAMPLE_PIN_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(EXAMPLE_PIN_BUTTON, GPIO_MODE_INPUT);
    gpio_pullup_en(EXAMPLE_PIN_BUTTON);

    // Setup I2C 0 for display
    ESP_LOGI(TAG, "Initialize I2C bus");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = true }
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    // Setup I2C 1 for sensors
    I2CChannel1.init(I2C_NUM_1, GPIO_NUM_33, GPIO_NUM_32);
    I2CChannel1.openAsMaster(100000);

    // Initialize sensors
    BMP280.init(I2CChannel1);
    SMP3011.init(I2CChannel1);

    // Initialize display
    i2c_oled_start(i2c_bus);
    
    // Create tasks
    xTaskCreate( TaskBlink  , "Blink"  , 1024,  nullptr,   tskIDLE_PRIORITY,  &taskBlinkHandle   );  
    xTaskCreate( TaskSensors, "Sensors", 2048,  nullptr,   tskIDLE_PRIORITY,  &taskSensorsHandle );    
    xTaskCreate( TaskDisplay, "Display", 4096,  nullptr,   tskIDLE_PRIORITY,  &taskDisplayHandle );

}

void TaskDisplay(void *parameter)
{
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(nullptr);
    
    // Create labels for pressure and temperature displays
    lv_obj_t *labelPressure = lv_label_create(scr);
    lv_label_set_long_mode(labelPressure, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(labelPressure, "");
    lv_obj_set_width(labelPressure, 128);
    lv_obj_align(labelPressure, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(labelPressure, &lv_font_montserrat_14, 0);
    

    lv_obj_t *labelTemperature = lv_label_create(scr);
    lv_label_set_long_mode(labelTemperature, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(labelTemperature, "");
    lv_obj_set_width(labelTemperature, 128);
    lv_obj_align(labelTemperature, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_text_font(labelTemperature, &lv_font_montserrat_14, 0);
    

    lv_obj_t *labelPressureAtm = lv_label_create(scr);
    lv_label_set_long_mode(labelPressureAtm, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(labelPressureAtm, "");
    lv_obj_set_width(labelPressureAtm, 128);
    lv_obj_align(labelPressureAtm, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_text_font(labelPressureAtm, &lv_font_montserrat_14, 0);
    

    lv_obj_t *labelTemperatureAtm = lv_label_create(scr);
    lv_label_set_long_mode(labelTemperatureAtm, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(labelTemperatureAtm, "");
    lv_obj_set_width(labelTemperatureAtm, 128);
    lv_obj_align(labelTemperatureAtm, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_text_font(labelTemperatureAtm, &lv_font_montserrat_14, 0);
    
    

    lvgl_port_unlock();

    while (1)
    {
        lvgl_port_lock(0);

        // Get sensor data
        float temperatureSMP3011 = SMP3011.getTemperature();
        float temperatureBMP280 = BMP280.getTemperature();
        float pressureSMP3011 = SMP3011.getPressure();
        float pressureBMP280 = BMP280.getPressure();

        // Switch between screens based on `currentScreen`
        switch (currentScreen) {
            case 0:  // Screen 1: Display Pressure and Temperature
                // Convert to PSI
                pressureSMP3011 = convertToPSI(pressureSMP3011);
                pressureBMP280 = convertToPSI(pressureBMP280);

                while pressureSMP3011 > nova : {
                    valor= pressureSMP3011;
                    sleep_ms(3000);
                    return valor;}

                lv_label_set_text_fmt(labelPressure, "%6.2f PSI", pressureSMP3011);
                lv_label_set_text_fmt(labelTemperature, "%6.2fC", temperatureSMP3011);
                break;
                
            case 1:  // Screen 2: Display Temperature in Celsius or Fahrenheit
                pressureSMP3011 = convertToBar(pressureSMP3011);
                pressureBMP280 = convertToBar(pressureBMP280);

                while pressureSMP3011 > nova:{
                    valor= pressureSMP3011;
                    sleep_ms(3000);
                    return valor;}

                lv_label_set_text_fmt(labelPressure, "%6.2f Bar", valor);
                lv_label_set_text_fmt(labelTemperature, "%6.2fF", convertToFahrenheit(temperatureSMP3011));
                break;
            
            case 2:  
                lv_label_set_text_fmt(labelPressure, "Projeto SE");
                lv_label_set_text_fmt(labelTemperature, "Fatec JDI");
                lv_label_set_text_fmt(labelTemperatureAtm, "Fatec JDI");
                break;
            
        }

        lvgl_port_unlock();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void TaskBlink(void *parameter)
{
    while(1)
    {
        gpio_set_level(EXAMPLE_PIN_LED, 1); 
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(EXAMPLE_PIN_LED, 0); 
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void TaskSensors(void *parameter)
{
    bool lastButtonState = true;  // Starts with the button not pressed (pull-up state)
    
    while (1)
    {
        // Read button state
        bool buttonState = gpio_get_level(EXAMPLE_PIN_BUTTON) == 0;  // Button pressed if level is 0
        
        // If the button was pressed, switch between screens
        if (buttonState && !lastButtonState) {
            currentScreen = (currentScreen + 1) % 3;  // Switch screens (0, 1, 2)
            ESP_LOGI(TAG, "Switching to Screen %d", currentScreen);
            vTaskDelay(200 / portTICK_PERIOD_MS);  // Debounce delay
        }

        lastButtonState = buttonState;

        BMP280.poll();
        SMP3011.poll();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}



