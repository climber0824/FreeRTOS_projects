#if CONFIG_FREERTOS_UNICORE
  static const BaseType_t app_cpu = 0;
#else
  static const BaseType_t app_cpu = 1;
#endif

// Settings
enum {NUM_TASKS = 5};           // number of tasks
enum {TASK_STACK_SIZE = 2048};  // bytes in ESP32

// Globals
static SemaphoreHandle_t bin_sem;   // wait for parameters to be read
static SemaphoreHandle_t done_sem;  // notifies main task when done
static SemaphoreHandle_t chopstick[NUM_TASKS];

// Tasks
void eat(void *parameters) {
  int num;
  char buf[50];
  int idx_1, idx_2;

  // Copy parameters and increse semaphore count
  num = *(int *)parameters;
  xSemaphoreGive(bin_sem);

  // Assign priority: pick up lower-number chopstick first
  if (num < (num + 1) % NUM_TASKS) {
    idx_1 = num;
    idx_2 = (num + 1) % NUM_TASKS;
  } else {
    idx_1 = (num + 1) % NUM_TASKS;
    idx_2 = num;
  }

  // Take lower-number chopstick
  xSemaphoreTake(chopstick[idx_1], portMAX_DELAY);
  sprintf(buf, "Philosopher %i took chopstick %i", num, num);
  Serial.println(buf);

  // Add some delay to force deadlock
  vTaskDelay(1 / portTICK_PERIOD_MS);

  // Take higher-number chopstick
  xSemaphoreTake(chopstick[idx_2], portMAX_DELAY);
  sprintf(buf, "Philosopher %i took chopstick %i", num, (num+1) % NUM_TASKS);  
  Serial.println(buf);

  // Eating
  sprintf(buf, "Philosopher %i is eating", num);
  Serial.println(buf);
  vTaskDelay(10 / portTICK_PERIOD_MS);

  // Put down higher-number chopstick
  xSemaphoreGive(chopstick[idx_2]);
  sprintf(buf, "Philosopher %i is return chopstick %i", num, (num+1) % NUM_TASKS);
  Serial.println(buf);

  // Put down lower-number chopstick
  xSemaphoreGive(chopstick[idx_1]);
  sprintf(buf, "Philosopher %i is return chopstick %i", num, num);
  Serial.println(buf);

  // Notify main task and delete self
  xSemaphoreGive(done_sem);
  vTaskDelete(NULL);
}

void setup() {
  char task_name[20];
  Serial.begin(115200);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  Serial.println();

  // Create kernel objects before starting tasks
  bin_sem = xSemaphoreCreateBinary();
  done_sem = xSemaphoreCreateCounting(NUM_TASKS, 0);
  for (int i = 0; i < NUM_TASKS; i++) {
    chopstick[i] = xSemaphoreCreateMutex();
  }

  // Have the philosopher start eating
  for (int i = 0; i < NUM_TASKS; i++) {
    sprintf(task_name, "Philosopher %i", i);
    xTaskCreatePinnedToCore(
      eat,
      task_name,
      TASK_STACK_SIZE,
      (void *)&i,
      1,
      NULL,
      app_cpu);

    xSemaphoreTake(bin_sem, portMAX_DELAY);
  }

  // Wait until all the philosophers are done
  for (int i = 0; i < NUM_TASKS; i++) {
    xSemaphoreTake(done_sem, portMAX_DELAY);
  }

  // If done, print
  Serial.println("Done! No deadlock");  
}

void loop() { 

}
