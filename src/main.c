
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"
#include "tusb.h"//the library used to create a serial port over USB, according to part 5 

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 

//Add here necessary states
enum state {WAITING=1, WRITE_TO_MEMORY=2, SEND_MESSAGE=3, UPPER_IDLE=4, UPPER_PROCESSING=5, MENU_IDLE, MENU_SEND, MENU_RECEIVE};


enum state menu_state= MENU_IDLE;
enum state lower_state = WAITING;
enum state upper_state = UPPER_IDLE;
char received_morse_code[256] = {0};//buffer to store 
bool message_received = false;
static volatile uint8_t button_pressed_1, button_pressed_2, ignore_buttons=false;






char current_morse;

char morse_message[257];

int morse_index = 0;

int measrement_device_index = 1;

int16_t sample_buffer[MEMS_BUFFER_SIZE];
int16_t temp_sample_buffer[MEMS_BUFFER_SIZE];//use to have two different buffers.z
volatile int samples_read = 0;

static void on_sound_buffer_ready(){
    samples_read = get_microphone_samples(sample_buffer, MEMS_BUFFER_SIZE);
}

static void read_mic() {
    if (init_microphone_sampling()<0){
        sleep_ms(500);
        printf("Cannot start sampling the microphone\n");
    
    end_microphone_sampling();
    get_microphone_samples(sample_buffer, MEMS_BUFFER_SIZE);
    for (int i = 0; i < sample_count; i++) {
        printf("%d\n", temp_sample_buffer[i]);
        sent_bytes += sizeof(temp_sample_buffer[0]);
    }
    }
}

static void read_accelerometer() {
    float ax, ay, az, gx, gy, gz, t;

    if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0)
    {
        if (az > 0.1) {
            printf("UP: %.2fg)\n", az); // delete after testing
            current_morse = '.';
        }
        else if (az < -0.1) {
            printf("DOWN: %.2fg)\n", az); // delete after testing
            current_morse = '-';
        }
        
    }
}




static void read_sensor(void *arg) {
    printf("read_sensor started %d\n", lower_state);
    (void) arg;
    
    while(1) {
        if (lower_state == WAITING) {
            switch (measrement_device_index) {
                case 1:
                    read_accelerometer();
                    break;
                case 2:
                    read_mic();
                    break;
        
        printf("lower state changed\n");
        lower_state = WRITE_TO_MEMORY;
                
        vTaskDelay(pdMS_TO_TICKS(1000)); 
            }
        }
    }
}








static void read_button(void *arg) {
    printf("read_button started %d\n", lower_state);
    (void) arg;
    while (1) {
        if (lower_state == WRITE_TO_MEMORY) {
            printf("passed state check \n");
            if (BUTTON1 != 0){   /// Writes the character to the message, if character is valid
                if (current_morse != '\0' && morse_index < 257) 
                {
                    morse_message[morse_index++] = current_morse;
                    morse_message[morse_index] = '\0'; /// 
                    printf("Stored: %c | Entire message: %s\n", current_morse, morse_message); /// only for testing
                }
            }
            else if (BUTTON2 != 0)
            {
                morse_message[morse_index++] = " ";
                morse_message[morse_index] = '\0'; /// 
                printf("Stored: %c | Entire message: %s\n", current_morse, morse_message); /// only for testing               
            }
            
                
            current_morse = '\0';
            lower_state = WAITING;  /// Ready for next motion
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

}




void tud_cdc_rx_cb(uint8_t itf){

    // allocate buffer for the data on the stack    
    uint8_t buf[CFG_TUD_CDC_RX_BUFSIZE + 1];

    uint32_t count = tud_cdc_n_read(itf, buf, sizeof(buf));// reads data from USB into buf. You’ll then process that data as needed. 

    if (itf == 1) {//add the data to received_morse_code
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
             
        
            else {vTaskDelay(pdMS_TO_TICKS(600));} //amount of ticks to indicate its a space
        }              
                
                
        //NOTES; kind of confused on how to differentiate spaces and the inherent timing between dots and dashes
    }}

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
                upper_state = MENU_SEND;
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
                upper_state = MENU_IDLE;
            }
            break;

        case MENU_RECEIVE:
            if (message_received) {
                upper_state = UPPER_PROCESSING;
            }
            else if (button_pressed_2) {
                button_pressed_2 = 0;
                upper_state = MENU_IDLE;
            }
            break;

        case UPPER_PROCESSING:
            morse_code_light(received_morse_code);
            clear_display();
            write_text_xy(0,0,"received:");
            vTaskDelay(pdMS_TO_TICKS(300));
            clear_display();
            write_text_xy(0,10,received_morse_code);
            vTaskDelay(pdMS_TO_TICKS(500));
            clear_display();
            write_text("waiting");
            message_received = false;
            upper_state = MENU_RECEIVE;
            break;
    }

    if (upper_state != last_state) {
        clear_display();
        switch (upper_state) {
            case MENU_IDLE:
                write_text_xy(0,0, "1:Send a Message");
                write_text_xy(0,10,"2:Receive a Message");
                break;
            case MENU_SEND:
                write_text_xy(0,0,"Send Mode");
                write_text_xy(0,10,morse_message);
                write_text_xy(0,20,"Press 2 to go back");
                break;
            case MENU_RECEIVE:
                write_text_xy(0,0,"Receive Mode");
                write_text_xy(0,10,"Press 2 to go back");
                break;
            case UPPER_PROCESSING:
                write_text_xy(0,0,"Processing...");
                break;
        }
        last_state = upper_state;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
}
}


int main() {
    stdio_init_all();
    // Uncomment this lines if you want to wait till the serial monitor is connected
    while (!stdio_usb_connected()){
        sleep_ms(10);
    } 
    /// start all components being used
    ICM42670_start_with_default_values();
    pdm_microphone_set_callback(on_sound_buffer_ready)

    printf("first print worked");
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
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
    result = xTaskCreate(display_task, "display_task", DEFAULT_STACK_SIZE, NULL, 2, &displayTask);             
    
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
