static const BastType_t pro_cpu = 0;
static const BastType_t app_cpu = 1;

static const char command[] = "avg";
static const uint16_t timer_divider = 8;          // Divide 80 MHz
static const uint64_t timer_max_count = 1000000;  // Timer counts to this value
static const uint32_t cli_delay = 10;             // ms delay
enum {BUF_LEN = 10};                              // Number of elements in sample buffer
enum {MSG_LEN = 100};                             // Max characters in message body
enum {MSG_QUEUE_LEN = 5};                         // Number of slots in message queue
enum {CMD_BUF_LEN = 255};                         // Number of characters in command buffer

static const int adc_pin = A0;

// Message struct to wrap strings for queue
typedef struct Message {
  char body[MSG_LEN];
} Message;

// Globals
static hw_timer_t *timer = NULL;
static TaskHandle_t processing_task = NULL;
static SemaphoreHandle_t sem_done_reading = NULL;
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t msg_queue;
static volatile uint16_t buf_0[BUF_LEN];      // One buffer in the pair
static volatile uint16_t buf_1[BUF_LEN];      // The other buffer in the pair
static volatile uint16_t *write_to = buf_0;   // Double buffer write pointer
static volatile uint16_t *read_from = buf_1;  // Double buffer read pointer
static volatile uint8_t buf_overrun = 0;      // Double buffer overrun flag
static float adc_avg;

// Swap the write_to and read_from pointers in the double buffer
// Only ISR calls this at the moment, so no need to make it thread-safe
void IRAM_ATTR swap() {
  volatile uint16_t *temp_ptr = write_to;
  write_to = read_from;
  read_from = temp_ptr;
}

// ISRs
// This function executes when timer reaches max and resets
void IRAM_ATTR onTimer() {
  static uint16_t idx = 0;
  BaseType_t task_woken = pdFALSE;

  // If buffer is not overrun,  read ADC to next buffer element.
  // Otherwise, drop the sample.
  if ((idx < BUF_LEN) && (buf_overrun == 0)) {
    write_to[idx] = analogRead(adc_pin);
    idx++;
  }

  // Check if the buffer is full
  if (idx >= BUF_LEN) {

    // If reading is not done, set overrun flag. We don't need to set tbis
    // as a critical section, as nothing can interrupt and change either value.
    if (xSemaphoreTakeFromISR(sem_done_reading, &task_woken) == pdFALSE) {
      buf_overrun = 1;
    }

    // Only swap buffers and notify task if overrun flag is cleared
    if (buf_overrun == 0) {

      // Reset index and swap buffer pointers
      idx = 0
      swap();

      // A task notification works link a binary semaphore but is faster
      vTaskNotifyGiveFromISR(processing_task, &task_woken);
    }
  }

  // Exit from ISR (Vanilla FreeRTOS)
  // portYIELD_FROM_ISR(task_woken);

  // Exit from ISR
  if (task_woken) {
    portYIELD_FROM_ISR();
  }
}


// Tasks
// Serial terminal task
void doCLI(void *parameters) {
  Message rcv_msg;
  char c;
  char cmd_buf[CMD_BUF_LEN];
  uint8_t idx = 0;
  uint8_t cmd_len = strlen(command);

  // Clear whole buffer
  memset(cmd_buf, 0, CMD_BUF_LEN);

  while (1) {

    // Look for any error message that need to be printed
    if (xQueueReceive(msg_queue, (void *)&rcv_msg, 0) == pdTRUE) {
      Serial.println(rcv_msg.body);
    }

    // Read characters from serial
    if (Serial.available() > 0) {
      c = Serial.read();

      if (idx < CMD_BUF_LEN - 1) {
        cmd_buf[idx] = c;
        idx++;
      }

      // Print newline and check input on 'enter'
      if ((c == '\n') || (c == '\r')) {
        Serial.print("\r\n");

        // Print average value if command is "avg"
        cmd_buf[idx - 1] = '\0';
        if (strcmp(cmd_buf, command) == 0) {
          Serial.print("Average: ");
          Serial.println(adc_avg);
        }

        // Reset receive buffer and index counter
        memset(cmd_buf, 0, CMD_BUF_LEN);
        idx = 0;
      }      
    } else {
      Serial.print(c);
    }

    // Yield CPU to other tasks
    vTaskDelay(cli_delay / portTICK_PERIOD_MS);
  }
}

// Wait for semaphore and calculate average of ADC values
void calcAverage(void *parameters) {
  Message msg;
  float avg;

  // Start a timer to run ISR per 100 ms
  timer = timerBegin(0, timer_divider, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, timer_max_count, true);
  timerAlarmEnable(timer);

  while (1) {

    // Wait for notification from ISR
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Calculate average
    avg = 0.0;
    for (int i = 0; i < BUF_LEN; i++) {
      avg += (float)read_from[i];
      vTaskDelay(105 / portTICK_PERIOD_MS); // Uncomment to test overrun flag
    }
    avg /= BUF_LEN;

    // Updating the shared float may or may not take multiple instructions,
    // so we protect it with a mutex or critical section. The ESP-IDF critical
    // section is the easiest for this application.
    portENTER_CRITICAL(&spinlock);
    adc_avg = avg;
    portEXIT_CRITICAL(&spinlock);

    // If we took too long to process, buffer writing will have overrun.
    // So we send a message to be printed out to the serial terminal.
    if (buf_overrun == 1) {
      strcpy(msg.body, "Error: Buffer overrun. Samples have been dropped." );
      xQueueSend(msg_queue, (void *)&msg, 10);
    }

    // Clearing the overrun flag and giving the "done reading" semaphore must
    // be done together without being interrupted.
    portENTER_CRITICAL(&spinlock);
    buf_overrun = 0;
    xSemaphoreGive(sem_done_reading);
    portEXIT_CRITICAL(&spinlock);
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  Serial.println();

  // Create semaphore before it is used (in task or ISR)
  sem_done_reading = xSemaphoreCreateBinary();

  // Force reboot if we can't create the semaphore
  if (sem_done_reading == NULL) {
    Serial.println("Could not create one or more semaphores");
    ESP.restart();
  }

  // The done reading semaphore initialize to 1
  xSemaphoreGive(sem_done_reading);

  // Create message queue before it is used
  msg_queue = xQueueCreate(MSG_QUEUE_LEN, sizeof(Message));

  // Start task to handle CLI events. It's at a higher priority
  // but only run it once per 10 ms.
  xTaskCreatePinnedToCore(
    doCLI,
    "Do CLI",
    1024,
    NULL,
    2,
    NULL,
    app_cpu
  );

  // Start task to calculate average. Save handle for use with
  // notifications. Set this task to run in core 0.
  xTaskCreatePinnedToCore(
    calcAverage,
    "Calculate average",
    1024,
    NULL,
    1,
    &processing_task,
    pro_cpu
  );

  vTaskDelete(NULL);
}

void loop() {
  
}
