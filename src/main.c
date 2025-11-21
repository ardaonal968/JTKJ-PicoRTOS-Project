
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"
#include "tusb.h"//the library used to create a serial port over USB, according to part 5 
#include <math.h>
// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 

//
//INSTRUCTIONS//
// turn up for dot, turn down for dash, straight for space.
// high light level for dot, low light level for dash.



//Add here necessary states
enum state {WAITING=1, WRITE_TO_MEMORY=2, SEND_MESSAGE=3, UPPER_IDLE=4, UPPER_PROCESSING=5, MENU_IDLE, MENU_SEND, MENU_RECEIVE, MENU_SEND_DEVICE_SELECT };
enum state menu_state= MENU_IDLE;
enum state lower_state = WAITING;
enum state upper_state = UPPER_IDLE;
char received_morse_code[256] = {0};//buffer to store 
bool message_received = false;
static volatile uint8_t button_pressed_1, button_pressed_2, ignore_buttons=false;

float light_lux=0;
int measurement_device_index = 1; // default: IMU


char current_morse;
char morse_message[257];
int morse_index = 0;
float gyroscope_data[10];
int gyroscope_data_index = 0;

int16_t sample_buffer[MEMS_BUFFER_SIZE];

int16_t temp_sample_buffer[MEMS_BUFFER_SIZE];//use to have two different buffers.z

volatile int samples_read = 0;

static void on_sound_buffer_ready(){
    samples_read = get_microphone_samples(sample_buffer, MEMS_BUFFER_SIZE);
}


static void read_accelerometer() {
    float ax, ay, az, gx, gy, gz, t;
    if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0)
    {
        if (ax > 0.1) {
            printf("UP: %.2fg)\n", ax); // delete after testing
            current_morse = '.';
        }
        else if (ax < -0.1) {
            printf("DOWN: %.2fg)\n", ax); // delete after testing
            current_morse = '-';
        }
        
    }
}

uint32_t read_light_sensor() {
    uint8_t txBuffer[1] = { VEML6030_ALS_REG };
    uint8_t rxBuffer[2] = {0, 0};
    float luxVal_uncorrected = 0;
    float luxVal =0;
    if (i2c_write_blocking(i2c_default, VEML6030_I2C_ADDR, txBuffer, 1, true) >= 0) {
        if (i2c_read_blocking(i2c_default, VEML6030_I2C_ADDR, rxBuffer, 2, false) >= 0) {
            printf("RX0: %02X RX1: %02X\n", rxBuffer[0], rxBuffer[1]);


            uint16_t raw =(((uint16_t)rxBuffer[1] << 8) | rxBuffer[0]) * 0.5376f;
            luxVal_uncorrected = raw * 0.5376f;
            printf("RAW: 0x%04X | RX0: %02X RX1: %02X\n", raw, rxBuffer[0], rxBuffer[1]);

        }
    }

    luxVal = luxVal_uncorrected; 

    if (luxVal_uncorrected > 1000.0f) {
        luxVal = (.00000000000060135f * powf(luxVal_uncorrected, 4)) - 
                 (.0000000093924f * powf(luxVal_uncorrected, 3)) + 
                 (.000081488f * powf(luxVal_uncorrected, 2)) + 
                 (1.0023f * luxVal_uncorrected);
    }

    // Set current_morse based on lux thresholds
    light_lux = (uint32_t)luxVal;

    if (luxVal > 200) current_morse = '.';
    else if (luxVal < 50) current_morse = '-';
    else current_morse = ' ';

    printf("Lux: %lu | current_morse: %c\n", light_lux, current_morse);

    return (uint32_t)luxVal;
}








void play_jingle(const uint32_t *notes, const uint32_t *durations) {
    for (uint32_t i = 0; notes[i] != 0; i++) {
        uint32_t duration = durations[i] *1;
        buzzer_play_tone(notes[i], duration);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

const uint32_t megalovania_notes[] = {294, 294, 587, 440, 415, 392, 349, 294, 349, 392,0};
    
//262, 262, 262, 262, 587, 440, 415, 392, 349, 294, 349,
  //  392, 247, 247, 587, 440, 415, 392, 349, 294, 349, 392, 233, 233, 233, 233, 587, 440, 415, 392, 349, 294, 349, 392,0 };

const uint32_t megalovania_durations[] = {125, 125, 250, 250, 125, 250, 250,125, 125, 125,0};
    
    /*62, 62, 62, 62,       
    250, 375, 125, 250, 250, 125, 125, 125, 
    125, 125, 250, 375, 125, 250, 250,   
    125, 125, 125, 62, 62, 62, 62,       
    250, 375, 125, 250, 250, 125, 125, 125,
    0  
};*/




/// NOT YET FULLY WORKING, PROTOTYPE BELOW

float read_gyro_memory() {
    float ax, ay, az, gx, gy, gz, t;
    float highest_value = 0;
    if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0)
    {
        gyroscope_data[gyroscope_data_index] = gz;
        gyroscope_data_index ++;
        for (int i = 0; i < 11; i++)
        {
            if (fabsf(gyroscope_data[i]) > fabsf(highest_value)){
                highest_value = gyroscope_data[i];
            }
        }
         
        if (gyroscope_data_index >= 9)
        {
            gyroscope_data_index = 0;
        }
        printf("gx value %f\n", gz);
        printf("gx value %f\n", highest_value);
        for (int i = 0; i < 10; i++)
        {
            printf("gyroscope data %f %d\n", gyroscope_data[i], i);
        }
         
    }
    return highest_value;
}


static void read_gyroscope() {
    float gx_highest = read_gyro_memory();
    if (gx_highest < 100 && gx_highest > 50) {
        current_morse = '.';
    }
    else if (gx_highest < -50 && gx_highest > -100) {
        current_morse = '-';
    }
    else if (gx_highest < 50 && gx_highest > -50) {
        current_morse = ' ';
    }        
    
    printf("gyroscope data %f \n", gx_highest);
}

static void read_orientation() {
    float ax, ay, az, gx, gy, gz, t;
    if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0)
    { 
        printf("ax: %f ay: %f az: %f others: %f %f %f\n", ax, ay, az,gx,gy,t);
        if (az >= 0.85){
            current_morse =  '.';
        }
        else if (az < -0.85)
        {
            current_morse = '-';   
        }
        else {
            current_morse = ' ';
        }  
    }
}



static void read_sensor(void *arg) {
    printf("read_sensor started %d\n", lower_state);
    (void) arg;
    
    while(1) {
        if (lower_state == WAITING && upper_state == MENU_SEND) {
            switch (measurement_device_index)
{
                case 1: 
                    read_light_sensor();
                    break;
                case 2:
                    read_orientation();
                    break;
                default:
                    read_orientation();
                    break;
}
                       
        printf("lower state changed\n");
        lower_state = WRITE_TO_MEMORY;
                
            }
        vTaskDelay(pdMS_TO_TICKS(100));   
        }}

static void read_button(void *arg) {
    printf("read_button started %d\n", lower_state);
    (void) arg;
    while (1) {
        if (lower_state == WRITE_TO_MEMORY) {
            printf("passed state check \n");
            if (button_pressed_1){   /// Writes the character to the message, if character is valid
                button_pressed_1 = 0;
                if (current_morse != '\0' && morse_index < 257) 
                {
                    morse_message[morse_index++] = current_morse;
                    morse_message[morse_index] = '\0'; /// 
                    printf("Stored: %c | Entire message: %s | BUTTON 1 %d and BUTTON 2 %d\n", current_morse, morse_message, BUTTON1, BUTTON2);
                }
            }
            else if (button_pressed_2)
            {
                button_pressed_2 = 0;
                morse_message[morse_index++] = ' ';
                morse_message[morse_index] = '\0'; /// 
                printf("Stored: %c | Entire message: %s\n", current_morse, morse_message); /// only for testing               
            }
            
                
            current_morse = '\0';
            lower_state = WAITING;  /// Ready for next motion
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

}




void tud_cdc_rx_cb(uint8_t itf){

    // allocate buffer for the data on the stack    
    uint8_t buf[CFG_TUD_CDC_RX_BUFSIZE + 1];

    uint32_t count = tud_cdc_n_read(itf, buf, sizeof(buf));// reads data from USB into buf. You’ll then process that data as needed. 

    if (itf == 0) {//add the data to received_morse_code
        for (int i = 0; i < count && i < sizeof(received_morse_code)-1; i++) {
            received_morse_code[i] = buf[i];}
        
        if (count < sizeof(received_morse_code)) {//terminate c string
            received_morse_code[count] = '\0';} 

        message_received = true;
 
        tud_cdc_n_write(itf, (uint8_t const *) "OK\n", 3); //be gentle and send an ok back
        tud_cdc_n_write_flush(itf);
    }

    // Optional: if you need a C-string, you can terminate it:
    // if (count < sizeof(buf)) buf[count] = '\0';
}


static void usbTask(void *arg) {
    (void)arg;
    while (1) {
        //printf("usbtask looping\n");
        tud_task();              // With FreeRTOS, wait for events
                                 // Do not add vTaskDelay.
    }
}


void morse_code_light(char* morse_code){//turn received morse into led light intervals

    for (int i=0; morse_code[i] !='\n' && morse_code[i] !='\0';i++){
        //message ends with two spaces and new line(\n) according to the doc so it should recognize it?
        if (morse_code[i] == ' ' && morse_code[i+1] == ' ' && morse_code[i+2] == '\n') {break;}


        if (morse_code[i] == '.') {
            set_led_status(true);
            vTaskDelay(pdMS_TO_TICKS(200));//amount of ticks to indicate its a dot
            set_led_status(false);
            vTaskDelay(pdMS_TO_TICKS(200));
        } 

        else if (morse_code[i] == '-') { 
            set_led_status(true);
            vTaskDelay(pdMS_TO_TICKS(600));//amount of ticks to indicate its a dash
            set_led_status(false);
            vTaskDelay(pdMS_TO_TICKS(200)); 
        }

        else if (morse_code[i] == ' ') {
            if (morse_code[i+1] == ' ') {
                vTaskDelay(pdMS_TO_TICKS(1400));}//amount of ticks to indicate space between two words
             
        
            else {vTaskDelay(pdMS_TO_TICKS(600));} //amount of ticks to indicate a gap between letters
        }              
    }
}



void morse_code_buzzer(char*morse_code){//turn received more code into buzzer sounds
    init_buzzer();

    for (int i=0; morse_code[i] !='\n' && morse_code[i] !='\0';i++){
        //message ends with two spaces and new line(\n) according to the doc so it should recognize it?
        if (morse_code[i] == ' ' && morse_code[i+1] == ' ' && morse_code[i+2] == '\n') {break;}


        if (morse_code[i] == '.') {
            buzzer_play_tone(440,100);
            vTaskDelay(pdMS_TO_TICKS(100));//amount of ticks to indicate its a dot
        } 

        else if (morse_code[i] == '-') { 
            buzzer_play_tone(440,400);
            vTaskDelay(pdMS_TO_TICKS(100));//amount of ticks to indicate its a dash
        }

        else if (morse_code[i] == ' ') {
            if (morse_code[i+1] == ' ') {
                vTaskDelay(pdMS_TO_TICKS(1400));}//amount of ticks to indicate space between two words
             
        
            else {vTaskDelay(pdMS_TO_TICKS(600));} //amount of ticks to indicate a gap between letters
        }              
    }
}

static void btn_fxn(uint gpio, uint32_t eventMask) {
    if (gpio  == BUTTON1)
        button_pressed_1 = true;
    else if (gpio == BUTTON2)
        button_pressed_2 = true;}



static void display_task(void *arg) {
    (void)arg;

    init_display();
    init_led();
    set_led_status(false);

    upper_state = MENU_IDLE;
    enum state last_state = 0;

    for (;;) {

    switch (upper_state) {

        case MENU_IDLE:
            if (button_pressed_1) {
                button_pressed_1 = 0;
                upper_state = MENU_SEND_DEVICE_SELECT;
            }
            else if (button_pressed_2) {
                button_pressed_2 = 0;
                upper_state = MENU_RECEIVE;
            }
            break;

        case MENU_SEND:
            if (button_pressed_2) {
                button_pressed_2 = 0;
                button_pressed_1 = 0;
                strcat(morse_message, "  \n");

                tud_cdc_n_write(0, (uint8_t const *)morse_message, strlen(morse_message));
                tud_cdc_n_write_flush(0);

                memset(morse_message,0,strlen(morse_message)); // taken from https://stackoverflow.com/questions/8107826/proper-way-to-empty-a-c-string, clears the string when we return to menu

                morse_index = 0;
                current_morse = '\0';
                lower_state = WAITING;
                upper_state = MENU_IDLE;
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            break;
        
        case MENU_SEND_DEVICE_SELECT: 
            if (button_pressed_1) {
                measurement_device_index = 1;
                button_pressed_1 = 0;
                button_pressed_2 = 0;
                upper_state = MENU_SEND;}   
            
            else if (button_pressed_2) { 
                measurement_device_index = 2;
                button_pressed_2 = 0;
                button_pressed_1 = 0;
                upper_state = MENU_SEND; }

        break;

        case MENU_RECEIVE:
            if (message_received && strlen(received_morse_code) <=25) {
                upper_state = UPPER_PROCESSING;
            }
            else if (message_received && strlen(received_morse_code) >25)
            {   
                clear_display();
                vTaskDelay(pdMS_TO_TICKS(100));
                write_text_xy(0,0,"Message too long!");
                vTaskDelay(pdMS_TO_TICKS(1000));
                message_received = false;
                upper_state = MENU_IDLE;
            }
            
            else if (button_pressed_2) {
                //play_jingle(megalovania_notes,megalovania_durations);
                button_pressed_2 = 0;
                upper_state = MENU_IDLE;
            }
            break;

        case UPPER_PROCESSING:
            morse_code_light(received_morse_code);
            vTaskDelay(pdMS_TO_TICKS(30));
            clear_display();

            // if len.received_morse_code > something : write writetext(a), writetext(b)
            write_text_xy(0,10,received_morse_code);
            morse_code_buzzer(received_morse_code);

            vTaskDelay(pdMS_TO_TICKS(500));
            clear_display();
            message_received = false;
            upper_state = MENU_RECEIVE;
            break;
    }

    if (upper_state != last_state) { // so that screen doesnt flicker
        clear_display();
        switch (upper_state) {
            case MENU_IDLE:
                write_text_xy(0,0, "1:Send a Message");
                write_text_xy(0,10,"2:Receive a Message");
                play_jingle(megalovania_notes,megalovania_durations);
                break;
            case MENU_SEND:
                write_text_xy(0,0,"Send Mode");
                write_text_xy(0,10,morse_message);
                write_text_xy(0,20,"Press 2 to go back");
                write_text_xy(0,30,"Press 1 to write");
                write_text_xy(0,40,"character");
                break;

            case MENU_SEND_DEVICE_SELECT:
                write_text_xy(0,0,"Press 1 for Light");
                write_text_xy(0,10,"Press 2 for IMU");
                break;
            
            case MENU_RECEIVE:
                write_text_xy(0,0,"Receive Mode");
                write_text_xy(0,10,"Press 2 to go back");
                break;
            case UPPER_PROCESSING:
                write_text_xy(0,0,"Processing");
                break;
        }
        last_state = upper_state;
    }
    else if (upper_state == MENU_SEND) // screen only updates if we are in MENU_SEND, so that the user can read their message
    {
        write_text_xy(0,0,"Send Mode");
        write_text_xy(0,10,morse_message);
        write_text_xy(0,20,"Press 2 to go back");
    }
    

    vTaskDelay(pdMS_TO_TICKS(100));
}
}


int main() {
    stdio_init_all();
    init_buzzer();


    // Uncomment this lines if you want to wait till the serial monitor is connected
    while (!stdio_usb_connected()){
        sleep_ms(10);
    } 
    printf("Start imc");
    /// start all components being used
    
    //pdm_microphone_set_callback(on_sound_buffer_ready);

    printf("first print worked");
    init_hat_sdk();
    init_i2c_default();

    sleep_ms(300); //Wait some time so initialization of USB and hat is done.
    ICM42670_start_with_default_values();
    init_veml6030();

    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_fxn);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_FALL, true, btn_fxn);
    printf("init successful");
    TaskHandle_t sensorTask, buttonTask, displayTask, hUsb = NULL;
    

    // Create the tasks with xTaskCreate
    //BaseType_t result = xTaskCreate(example_task,       // (en) Task function
    //            "example",              // (en) Name of the task 
    //            DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
    //            NULL,               // (en) Arguments of the task 
    //            2,                  // (en) Priority of this task
    //            &myExampleTask);    // (en) A handle to control the execution of this task

    // Create the sensor task
    BaseType_t result = xTaskCreate(read_sensor, "read_sensor", DEFAULT_STACK_SIZE, NULL, 2, &sensorTask);
    if (result != pdPASS) {
        printf("Sensor Task creation failed\n");
        return 0;
    }
    printf("readsensor");
    // Create the button task
    result = xTaskCreate(read_button, "read_button", DEFAULT_STACK_SIZE, NULL, 2, &buttonTask);
    if (result != pdPASS) {
        printf("Button Task creation failed\n");
        return 0;
    }
    printf("readbutton");



    // Create the display task
    result = xTaskCreate(display_task, "display_task", DEFAULT_STACK_SIZE, NULL, 3, &displayTask);             
    
    if (result != pdPASS) {
        printf("Display Task creation failed\n");
        return 0;
    }

    // Create the usb task
    result = xTaskCreate(usbTask, "usb_task", DEFAULT_STACK_SIZE, NULL, 3, &hUsb);//priority 3

    if (result != pdPASS) {
        printf("usb task creation failed\n");
        return 0;
    }


    // Start the scheduler
    tusb_init();
    vTaskStartScheduler();

    while (1){
        printf("failed :(");
    }
    // Never reach this line
    return 0;
}
