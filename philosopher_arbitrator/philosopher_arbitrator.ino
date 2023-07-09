#include CONFIG_FREERTOS_UNICORE
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
static SemaphoreHandle_t arbitrator // Controls who eats at when

// Task
void eat(void *parameters) {
  int num;
  char buf[50];

  // Copy parameter and increase semaphore count
  num = *(int *)parameters;
  xSemaphoreGive(bin_sem);

  // Wait our turn to eat
  xSemaphoreTake(arbitrator, portMAX_DELAY);
  
  // Take left chopstick
  xSemaphoreTake(chopstick[num], portMAX_DELAY);
  sprintf(buf, "Philosopher %i took chopstick %i", num, num);
  Serial.println(buf);

  // Add some delay to force deadlock
  vTaskDelay(1 / portTICK_PERIOD_MS);

  // Take left chopstick
  xSemaphoreTake(chopstick[(num+1) % NUM_TASKS], portMAX_DELAY);
  sprintf(buf, "Philosopher %i took chopstick %i", num, (num+1) % NUM_TASKS);  
  Serial.println(buf);

  // Eating
  sprintf(buf, "Philosopher %i is eating", num);
  Serial.println(buf);
  vTaskDelay(10 / portTICK_PERIOD_MS);

  // Put down right chopstick
  xSemaphoreGive(chopstick[(num+1) % NUM_TASKS]);
  sprintf(buf, "Philosopher %i returned chopstick %i", num, (num+1) % NUM_TASKS);
  Serial.println(buf);

  // Put down left chopstick
  xSemaphoreGive(chopstick[num]);
  sprintf(buf, "Philosopher %i returned chopstick %i", num, num);
  Serial.println(buf);

  // Tell the arbitrator that we're done
  xSemaphoreGive(arbitrator);

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
  arbitrator = xSemaphoreCreateMutex();

  // Have the philosphers start eating
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

  // If done
  Serial.println("Done! No deadlock");  
}

void loop() {
  
}
