#if CONFIG_FREERTOS_UNICORE
  static const BaseType_t app_cpu = 0;
#else
  static const BaseType_t app_cpu = 1;
#endif

// Settings
static const TickType_t dim_delay = 5000 / portTICK_PERIOD_MS;
static const int led_pin = LED_BUILTIN;
static TimerHandle_t one_shot_timer = NULL;

// Callbacks

// Turn off LED when timer expires
void autoDimmerCallback(TimerHandle_t xTimer) {
  digitalWrite(led_pin, LOW);
}

// Tasks

// Echo things back to serial port, turn on LED when while entering input
void doCLI(void *parameters) {
  char c;
  pinMode(led_pin, OUTPUT);

  while (1) {
    if (Serial.available() > 0) {
      c = Serial.read();
      Serial.print(c);

      digitalWrite(led_pin, HIGH);
      
      // Start timer (if timer is already running, this will act as
      // xTimerReset() instead)
      xTimerStart(one_shot_timer, portMAX_DELAY);
    }
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  Serial.println();

  // Create an one-shot timer
  one_shot_timer = xTimerCreate(
    "One-shot timer",
    dim_delay,          // Period of timer (in ticks)
    pdFALSE,            // Auto-reload
    (void *)0,          // Timer ID
    autoDimmerCallback  // Callback function
  );

  // Start CLI task
  xTaskCreatePinnedToCore(
    doCLI,
    "Do CLI",
    1024,
    NULL,
    1,
    NULL,
    app_cpu
  );

  vTaskDelete(NULL);
}

void loop() {

}
