#if CONFIG_FREERTOS_UNICORE
  static const BastType_t app_cpu=0;
#else
  static const BaseType_t app_cpu=1;
#endif

static const uint8_t buf_len = 255;
static char *msg_ptr = NULL;
static volatile uint8_t msg_flag = 0;

// Read message from serial buffer
void readSerial(void *parameter) {
  char c;
  char buf[buf_len];
  uint8_t idx = 0;

  // Clear whole buffer
  memset(buf, 0, buf_len);

  while (1) {
    // Read char from serial
    if (Serial.available() > 0) {
      c = Serial.read();

      // Store char into buffer
      if (idx < buf_len - 1) {
        buf[idx] = c;
        idx++;
      }

      
      if (c == '\n') {
        buf[idx-1] = '\0';

        // Try to allocate memory and copy over message. If message buffer is
        // still in use, ignore the entire message.
        if (msg_flag == 0) {
          msg_ptr = (char *)pvPortMalloc(idx * sizeof(char));

          // If malloc return 0 (out of memory), throw an error and reset
          configASSERT(msg_ptr);

          memcpy(msg_ptr, buf, idx);

          // Notify other task that message is ready
          msg_flag = 1;
        }

        // Reset receive buffer and index counter
        memset(buf, 0, buf_len);
        idx = 0;
      }
    }
  }
}

// Print message whenever flag is set and free buffer
void printMessage(void *parameter) {
  while (1) {
    // Wait for flag to be set and print message
    if (msg_flag == 1) {
      Serial.println(msg_ptr);
      Serial.print("Free heap (bytes): ");
      Serial.println(xPortGetFreeHeapSize());

      // Free buffer, set pointer to null, and clear flag
      vPortFree(msg_ptr);
      msg_ptr = NULL;
      msg_flag = 0;
    }
  }
}

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}
