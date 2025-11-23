
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"
#include "tusb.h"//the library used to create a serial port over USB
#include <math.h>
// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048 


//INSTRUCTIONS//
// turn up for dot, turn down for dash, straight for space.
// high light level for dot, dim light level for space, low light level for dash.

// Function delegation
// Arda: read_light_sensor, receive_message, morse_code_light, morse_code_buzzer, display_task
// Iivari:read_orientation, read_sensor, read_button, 


//// Machine works by having two state machines, one for recieving the messages from the workstation (upper_state), 
//// and one for sending messages (lower_state). The workload was mostly divided into lower state for Iivari and
//// upper for Arda, but both parties worked on both. 


///Lower state only activates when the upper state machine is in MENU_SEND.

enum state {WAITING=1, WRITE_TO_MEMORY=2, SEND_MESSAGE=3, UPPER_IDLE=4, UPPER_PROCESSING=5, MENU_IDLE, MENU_SEND, MENU_RECEIVE, MENU_SEND_DEVICE_SELECT };
enum state lower_state = WAITING;
enum state upper_state = UPPER_IDLE;
char received_morse_code[256] = {0};//buffer to store 
bool message_received = false;
int receive_number = 0;// tracks index to increment positions in receive_morse
static volatile uint8_t button_pressed_1, button_pressed_2, ignore_buttons=false;

float light_lux=0;
int measurement_device_index = 1; // default: IMU

const uint32_t megalovania_notes[] = {294, 294, 587, 440, 415, 392, 349, 294, 349, 392,0};//notes for the jingle
const uint32_t megalovania_durations[] = {125, 125, 250, 250, 125, 250, 250,125, 125, 125,0};


char current_morse;
char morse_message[257];
int morse_index = 0;
float gyroscope_data[10];
int gyroscope_data_index = 0;

volatile int samples_read = 0;


uint32_t read_light_sensor() { //made by Arda, reads light sensor

    uint8_t txBuffer[1] = {VEML6030_ALS_REG};
    uint8_t rxBuffer[2] = {0, 0};

    float luxVal_uncorrected = 0;
    float luxVal =0;

    if (i2c_write_blocking(i2c_default, VEML6030_I2C_ADDR, txBuffer, 1, true) >= 0) {
        if (i2c_read_blocking(i2c_default, VEML6030_I2C_ADDR, rxBuffer, 2, false) >= 0) {


            uint16_t raw =(((uint16_t)rxBuffer[1] << 8) | rxBuffer[0]) * 0.5376f;
            luxVal_uncorrected = raw * 0.5376f;


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

    if (luxVal > 150) current_morse = '.'; // high level for dot
    else if (luxVal < 10) current_morse = '-'; // low level for dash
    else current_morse = ' '; // dim lighting for space


    return (uint32_t)luxVal;
}


static void read_orientation() { /// made by iivari, used to read the current orientation of the device. 
    float ax, ay, az, gx, gy, gz, t;
    if (ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &t) == 0)
    { 
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



static void read_sensor(void *arg) {  ///Made by iivari, a switch case to swap between the different types of sensors that then produce the morse code
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
                       
        lower_state = WRITE_TO_MEMORY;
                
            }
        vTaskDelay(pdMS_TO_TICKS(100));   
        }}

static void read_button(void *arg) { 
    (void) arg;
    while (1) {
        if (lower_state == WRITE_TO_MEMORY) {
            if (button_pressed_1){   /// Writes the character to the message, if character is valid
                button_pressed_1 = 0;
                if (current_morse != '\0' && morse_index < 257) 
                {
                    morse_message[morse_index++] = current_morse;
                    morse_message[morse_index] = '\0'; 
                }
            }
            
                
            current_morse = '\0';
            lower_state = WAITING;  /// Ready for next motion
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

}

static void play_jingle(const uint32_t *notes, const uint32_t *durations) {
    for (uint32_t i = 0; notes[i] != 0; i++) {
        // for all notes, play the buzzer
        buzzer_play_tone(notes[i], durations[i]);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}


static void btn_fxn(uint gpio, uint32_t eventMask) {
    // Button handler //
    if (gpio  == BUTTON1)
        button_pressed_1 = true;
    else if (gpio == BUTTON2)
        button_pressed_2 = true;}



static void receive_message(void *pvParameters) {
    (void)pvParameters; 

    while (1) {
        int getchar = getchar_timeout_us(0);
        // non-blocking, look for characters from workstation

        if (getchar >= 0) {
            // if character found
        
            char received_character = (char)getchar;

            if (received_character != '\n' && receive_number < sizeof(received_morse_code) - 1) 
            // if received character isn't a newline, and there is space in the buffer
            {
                received_morse_code[receive_number++] = received_character;}
                // append the character to the received_morse_code, incremented by receive


            else if (received_character == '\n') {
                
                if (receive_number > 0){// check for empty messages
                    received_morse_code[receive_number] = '\0';
                    message_received = true;// flag for display_task to work
                    receive_number = 0;// reset index
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
}}


static void usbTask(void *arg) {
    (void)arg;
    while (1) {
        tud_task();              // With FreeRTOS, wait for events
                                 // Do not add vTaskDelay.
    }
}


void morse_code_light(char* morse_code){//turn received morse into led light intervals

    for (int i=0; morse_code[i] !='\n' && morse_code[i] !='\0';i++){
        //go through all morse code
        if (morse_code[i] == ' ' && morse_code[i+1] == ' ' && morse_code[i+2] == '\n') {break;}
        //message ends with two spaces and new line(\n) according to the doc so it should recognize it?



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
        //go through all morse code
        if (morse_code[i] == ' ' && morse_code[i+1] == ' ' && morse_code[i+2] == '\n') {break;}
        //message ends with two spaces and new line(\n) according to the doc so it should recognize it?



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

static void display_task(void *arg) { 
    // LCD display configuration //
    (void)arg;

    init_display();
    init_led();
    set_led_status(false);

    upper_state = MENU_IDLE;
    enum state last_state = 0;

    for (;;) {

    switch (upper_state) {
    //handles user input and state transitions, UI elements implemented on a non-looping iteration.

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
                strcat(morse_message, "  \n");
                // append two empty spaces and line break before sending, not sure if necessary?
                printf("%s",morse_message);
                clear_display();
                write_text_xy(0,0,"Message Sent!");
                buzzer_play_tone(500, 200);
                vTaskDelay(pdMS_TO_TICKS(1000));


                memset(morse_message,0,sizeof(morse_message)); // taken from https://stackoverflow.com/questions/8107826/proper-way-to-empty-a-c-string, clears the string when we return to menu

                morse_index = 0;
                current_morse = '\0';
                lower_state = WAITING;
                upper_state = MENU_IDLE;
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            break;
        
        case MENU_SEND_DEVICE_SELECT:

            if (button_pressed_1) {
                button_pressed_1 = 0;
                button_pressed_2 = 0;
                measurement_device_index = 1;
                upper_state = MENU_SEND;}   
            
            else if (button_pressed_2) { 
                button_pressed_1 = 0;
                button_pressed_2 = 0;
                measurement_device_index = 2;
                upper_state = MENU_SEND; }

        break;

        case MENU_RECEIVE:

            if (message_received && strlen(received_morse_code) <=100) {

                upper_state = UPPER_PROCESSING;
            }
            else if (message_received && strlen(received_morse_code) >100)
            {   
                clear_display();
                vTaskDelay(pdMS_TO_TICKS(100));
                write_text_xy(0,0,"Message too long!");
                vTaskDelay(pdMS_TO_TICKS(1000));
                memset(received_morse_code, 0, sizeof(received_morse_code));
                message_received = false;
                upper_state = MENU_IDLE;
            }
            
            else if (button_pressed_2) {
                upper_state = MENU_IDLE;
            }
            break;

        case UPPER_PROCESSING:

            //printf("received_morse_code;%s\n",received_morse_code);    
            vTaskDelay(pdMS_TO_TICKS(30));
            clear_display();

            int len = strlen(received_morse_code);
    
            char line1[21] = {0};                            // logic to split the buffer into 4, done with help from ai model deepseek V3,
            strncpy(line1, received_morse_code, 20);         // with prompt "what's a simple way to split my buffer into four lines so it doesn't exceed lcd line limits?",
                                                             // given in tandem with my upper processing task and received morse code buffer
            write_text_xy(0,10, line1);             

            if (len > 20) {
                char line2[21] = {0};
                strncpy(line2, received_morse_code + 20, 20);
                write_text_xy(0,20, line2);}

            if (len > 40) {
                char line3[21] = {0};
                strncpy(line3, received_morse_code + 40, 20);
                write_text_xy(0,30, line3);}

            if (len > 60) {
                char line4[21] = {0};
                strncpy(line4, received_morse_code + 60, 20);
                write_text_xy(0,40, line4);}

            // if len.received_morse_code > something : write writetext(a), writetext(b)
            // write_text_xy(0,10,received_morse_code);
            morse_code_buzzer(received_morse_code);
            morse_code_light(received_morse_code);

            vTaskDelay(pdMS_TO_TICKS(500));
            clear_display();
            memset(received_morse_code, 0, sizeof(received_morse_code));
            message_received = false;
            upper_state = MENU_RECEIVE;
            break;
    }

    if (upper_state != last_state) {
        // Implemented because of problems regarding screen flickering, non looping iteration of the task
        button_pressed_1 = 0;// When there is a state transition, reset buttons
        button_pressed_2 = 0;
        clear_display();
        switch (upper_state) {  /// swaps between the different screens of the lcd based on the current position of the upper state machine
            case MENU_IDLE:
                write_text_xy(40,0,"Welcome!");
                write_text_xy(0,10, "1:Send a Message");
                write_text_xy(0,20,"2:Receive a Message");
                play_jingle(megalovania_notes,megalovania_durations);
                break;
            case MENU_SEND:
                
                write_text_xy(40,0,"Send Mode");
                write_text_xy(0,20,"2:Back to Menu");
                write_text_xy(0,30,"1:Write Character");
                break;

            case MENU_SEND_DEVICE_SELECT:
                button_pressed_1 = 0;
                button_pressed_2 = 0;
                write_text_xy(0,0,"1:Light Sensor Mode");
                write_text_xy(0,10,"2:IMU Mode");
                break;
            
            case MENU_RECEIVE:
                write_text_xy(30,0,"Receive Mode");
                write_text_xy(0,20,"Waiting for Input!");
                write_text_xy(0,30,"2:Back to Menu");
                break;
            case UPPER_PROCESSING:
                write_text_xy(0,0,"Processing");
                break;
        }
        last_state = upper_state; /// puts into memory the last state, so that we dont keep updating the screen, see "if (upper_state != last_state)"
    }
    else if (upper_state == MENU_SEND)
    // screen only updates if we are in MENU_SEND, so that there is real time displayment of the morse message
    {
        write_text_xy(0,10,morse_message);
    }
    

    vTaskDelay(pdMS_TO_TICKS(10));
}
}


int main() {
    stdio_init_all();
    init_buzzer();


    // Uncomment this lines if you want to wait till the serial monitor is connected
    while (!stdio_usb_connected()){
        sleep_ms(10);
    } 

    
    init_hat_sdk();
    init_i2c_default();

    sleep_ms(300); //Wait some time so initialization of USB and hat is done.
    ICM42670_start_with_default_values();
    init_veml6030();

    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_RISE, true, btn_fxn);
    //printf("init successful");
    TaskHandle_t sensorTask, buttonTask, receiveTask, displayTask, hUsb = NULL;
    

    // Create the sensor task
    BaseType_t result = xTaskCreate(read_sensor, "read_sensor", DEFAULT_STACK_SIZE, NULL, 2, &sensorTask);
    if (result != pdPASS) {
        printf("Sensor Task creation failed\n");
        return 0;
    }
    // Create the receive task
    result = xTaskCreate(receive_message, "receive_message", DEFAULT_STACK_SIZE,NULL, 2,  &receiveTask);
    if (result != pdPASS) {
        printf("Receive Task creation failed\n");
        return 0;
    }

    // Create the button task
    result = xTaskCreate(read_button, "read_button", DEFAULT_STACK_SIZE, NULL, 2, &buttonTask);
    if (result != pdPASS) {
        printf("Button Task creation failed\n");
        return 0;
    }
    //printf("readbutton");


    // Create the display task
    result = xTaskCreate(display_task, "display_task", DEFAULT_STACK_SIZE, NULL, 3, &displayTask);             
    
    if (result != pdPASS) {
        printf("Display Task creation failed\n");
        return 0;
    }

    // Create the usb task
    result = xTaskCreate(usbTask, "usb_task", DEFAULT_STACK_SIZE, NULL, 4, &hUsb);//priority 3

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
