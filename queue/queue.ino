#if CONFIG_FREERTOS_UNICORE
  static const BastType_t app_cpu=0;
#else
  static const BaseType_t app_cpu=1;
#endif

static const uint8_t buf_len = 255;
static const char command[] = "delay ";
static const int delay_queue_len = 5;
static const int msg_queue_len = 5;
static const uint8_t blink_max = 100;
static const int led_pin = LED_BUILTIN;

// Message struct: used to wrap strings
typedef struct Message {
  char body[20];
  int count;
} Message;

// Globals
static QueueHandle_t delay_queue;
static QueueHandle_t msg_queue;

// Tasks: command line interface
void doCLI(void *parameters) {
  Message rcv_msg;
  char c;
  char buf[buf_len];
  uint8_t idx = 0;
  uint8_t cmd_len = strlen(command);
  int led_delay;

  // Clear whole buffer
  memset(buf, 0, buf_len);

  while (1) {

    // See if there is a message in the queue
    if (xQueueReceive(msg_queue, (void *)&rcv_msg, 0) == pdTRUE) {
      Serial.print(rcv_msg.body);
      Serial.println(rcv_msg.count);
    }

    // Read chars from serial
    if (Serial.available() > 0) {
      c = Serial.read();
      
      // Store chars in buffer
      if (idx < buf_len - 1)
        buf[idx++] = c;
      
      // Print  newline and check input on 'enter'
      if ((c == '\n') || (c == '\r')) {

        // Print newline to terminal
        Serial.print("\r\n");

        // Check if the firest 6 chars are "delay"
        if (memcpy(buf, command, cmd_len) == 0) {

          // Convert last part to positive int
          char *tail = buf + cmd_len;
          led_delay = atoi(tail);
          led_delay = abs(led_delay);

          // Send int to other task via queue
          if (xQueueSend(delay_queue, (void *)&led_delay, 10) != pdTRUE) {
            Serial.println("Error: Could not put item on delay queue.");
          }
        }

        // Reset receive buffer and index counter
        memset(buf, 0, buf_len);
        idx = 0;

      // otherwise, echo char back to serial terminal
      } else {
        Serial.print(c);
      }   
    } 
  }
}

// Task: flash LEd based on delay provided, notify other task every 100 blinks
void blinkLED(void *parameters) {
  Message msg;
  int led_delay = 500;
  uint8_t counter = 0;
  pinMode(LED_BUILTIN, OUTPUT);

  while (1) {
    // See if there is a message in the queue
    if (xQueueReceive(delay_queue, (void *)&led_delay, 0) == pdTRUE) {
      // Use only one task to manage serial comms
      strcpy(msg.body, "Message received ");
      msg.count = 1;
      xQueueSend(msg_queue, (void *)&msg, 10);
    }

    // Blink
    digitalWrite(led_pin, HIGH);
    vTaskDelay(led_delay / portTICK_PERIOD_MS);
    digitalWrite(led_pin, LOW);
    vTaskDelay(led_delay / portTICK_PERIOD_MS);

    // If we've blinked 100 times, send a message to other task
    counter++;
    if (counter >= blink_max) {
      // Construct message and send
      strcpy(msg.body, "Blinked: ");
      msg.count = counter;
      xQueueSend(msg_queue, (void *)&msg, 10);

      // Reset counter
      counter = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  Serial.println();
  Serial.println("Enter the command 'delay xxx' where xxx is your desired ");
  Serial.println("LED blink delay time in ms");

  // Create queues
  delay_queue = xQueueCreate(delay_queue_len, sizeof(int));
  msg_queue = xQueueCreate(msg_queue_len, sizeof(Message));

  // Start CLI task
  xTaskCreatePinnedToCore(
    doCLI, 
    "CLI", 
    1024,
    NULL,
    1,
    NULL,
    app_cpu);

  // Start blink task
  xTaskCreatePinnedToCore(
    blinkLED,
    "Blink LED",
    1024,
    NULL,
    1,
    NULL,
    app_cpu);

  vTaskDelete(NULL);
}

void loop() {
  
}