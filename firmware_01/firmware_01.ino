#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#define TASK_PERIOD_MS      5000    //  Rechecking time gap for currpuption and repair in firmware
#define WATCHDOG_TIMEOUT_MS 30000   //  Having margin over the max time the repair could take
#define COPY_CHUNK_SIZE     4096    //  chunk sizes for partition copy and repair

// task handles
TaskHandle_t firmwareTaskHandle     =   NULL;
TaskHandle_t basicOperationHandle   =   NULL;

struct OtaSlot {
    const esp_partition_t* partition;
    bool valid;
    const char* label;
};

// ------------------------------------------------------------------
// 1. Scan all three OTA partitions using esp_partition_get_sha256()
// ------------------------------------------------------------------
bool scanAllOtaPartitions(OtaSlot slots[3]) {
    Serial.println("[INFO]       Scaning all partitions.");
    const esp_partition_subtype_t subtypes[3] = {
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        ESP_PARTITION_SUBTYPE_APP_OTA_1,
        ESP_PARTITION_SUBTYPE_APP_OTA_2
    };
    const char* labels[3] = {"ota_0", "ota_1", "ota_2"};
    bool any_valid = false;

    for (int i = 0; i < 3; i++) {
        slots[i].partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtypes[i], NULL);
        slots[i].label = labels[i];
        if (!slots[i].partition) {
            Serial.printf("[ERROR]      Partition %s not found!\n", labels[i]);
            slots[i].valid = false;
            continue;
        }

        uint8_t sha256[32];
        esp_err_t err = esp_partition_get_sha256(slots[i].partition, sha256);
        if (err == ESP_OK) {
            slots[i].valid = true;
            any_valid = true;
            Serial.printf("[INFO]       %s is VALID\n", labels[i]);
        } else {
            slots[i].valid = false;
            Serial.printf("[ERROR]      %s is INVALID (err: %s)\n", labels[i], esp_err_to_name(err));
        }
    }
    return any_valid;
}

// ------------------------------------------------------------------
// 2. Copy entire partition content from src to dst (destructive)
// ------------------------------------------------------------------
bool repairPartition(const esp_partition_t* dst, const esp_partition_t* src) {
    if (!dst || !src) {
        Serial.println("[ERROR]     repairPartition: null partition pointer");
        return false;
    }
    if (dst->size != src->size) {
        Serial.println("[ERROR]     repairPartition: partition size mismatch");
        return false;
    }

    Serial.printf("[INFO]       Repairing %s <- %s (size: %lu bytes)\n",
                  dst->label, src->label, dst->size);

    // 1. Erase destination
    esp_err_t err = esp_partition_erase_range(dst, 0, dst->size);
    if (err != ESP_OK) {
        Serial.printf("[ERROR]      Erase failed: %s\n", esp_err_to_name(err));
        return false;
    }

    // 2. Copy in chunks
    uint8_t* buffer = (uint8_t*)malloc(COPY_CHUNK_SIZE);
    if (!buffer) {
        Serial.println("[ERROR]     Failed to allocate copy buffer");
        return false;
    }

    size_t remaining = dst->size;
    size_t offset = 0;
    bool success = true;

    while (remaining > 0) {
        size_t chunk = (remaining < COPY_CHUNK_SIZE) ? remaining : COPY_CHUNK_SIZE;

        // Feed watchdog before each chunk operation (to prevent timeout during long copy)
        esp_task_wdt_reset();

        // Read from source
        err = esp_partition_read(src, offset, buffer, chunk);
        if (err != ESP_OK) {
            Serial.printf("[ERROR]      Read failed at offset 0x%X: %s\n", offset, esp_err_to_name(err));
            success = false;
            break;
        }

        // Write to destination
        err = esp_partition_write(dst, offset, buffer, chunk);
        if (err != ESP_OK) {
            Serial.printf("[ERROR]      Write failed at offset 0x%X: %s\n", offset, esp_err_to_name(err));
            success = false;
            break;
        }

        remaining -= chunk;
        offset += chunk;

        // Feed watchdog if needed (optional)
        // vTaskDelay(pdMS_TO_TICKS(1));
    }

    free(buffer);

    if (success) {
        // Verify the copy by re-checking SHA256
        uint8_t sha[32];
        err = esp_partition_get_sha256(dst, sha);
        if (err == ESP_OK) {
            Serial.printf("[INFO]       Repair successful, %s is now VALID\n", dst->label);
            return true;
        } else {
            Serial.printf("[ERROR]      Repair verification failed: %s\n", esp_err_to_name(err));
            return false;
        }
    }
    return false;
}

// ------------------------------------------------------------------
// 3. Repair all invalid OTA partitions (except the running one)
// ------------------------------------------------------------------
void repairAllInvalidPartitions(OtaSlot slots[3], const esp_partition_t* current) {
    // Find the first valid partition to use as source
    const esp_partition_t* source = NULL;
    for (int i = 0; i < 3; i++) {
        if (slots[i].valid && slots[i].partition != current) {
            source = slots[i].partition;
            break;
        }
    }

    if (!source) {
        Serial.println("[ERROR]     No other valid partition found -> cannot repair");
        return;
    }

    Serial.printf("[INFO]       Using %s as source for repairs\n", source->label);

    // Repair any invalid partition (including the running one? NO – can't repair self)
    for (int i = 0; i < 3; i++) {
        if (!slots[i].valid && slots[i].partition != current) {
            Serial.printf("[INFO]       Attempting to repair invalid partition: %s\n", slots[i].label);
            if (repairPartition(slots[i].partition, source)) {
                // Update local validity
                slots[i].valid = true;
            } else {
                Serial.printf("[ERROR]      Failed to repair %s\n", slots[i].label);
            }
        }
    }
}

// ------------------------------------------------------------------
// 4. Determine next boot partition (unchanged from original)
// ------------------------------------------------------------------
const esp_partition_t* getNextValidPartition(OtaSlot slots[3]) {
    const esp_partition_t* current = esp_ota_get_running_partition();
    int current_index = -1;
    for (int i = 0; i < 3; i++) {
        if (slots[i].partition == current) {
            current_index = i;
            break;
        }
    }

    // Case 1: current unknown or invalid – pick first valid
    if (current_index == -1 || !slots[current_index].valid) {
        Serial.println("[INFO]      Current firmware invalid/unknown -> searching for valid image...");
        for (int i = 0; i < 3; i++) {
            if (slots[i].valid) {
                Serial.printf("[INFO]       Switching to %s\n", slots[i].label);
                return slots[i].partition;
            }
        }
        return NULL;
    }

    // Case 2: current valid – round‑robin to next valid
    Serial.printf("[INFO]       Current partition: %s (valid)\n", slots[current_index].label);
    for (int offset = 1; offset <= 3; offset++) {
        int next_index = (current_index + offset) % 3;
        if (slots[next_index].valid) {
            Serial.printf("[INFO]       Next valid partition: %s\n", slots[next_index].label);
            return slots[next_index].partition;
        }
    }

    Serial.println("[INFO]      No other valid partition found -> staying on current.");
    return NULL;
}

// ------------------------------------------------------------------
// 5. Main firmware task
// ------------------------------------------------------------------
void firmwareTask(void* pvParameters) {
    
    esp_task_wdt_add(NULL);  // NULL current task

    Serial.println("\n\n");
    Serial.println("-----------------------------------------------------------------------------");
    Serial.println("                         Running Firmware Control Task:                      ");
    Serial.println("-----------------------------------------------------------------------------");
    Serial.println("\n\n");
    // --- Boot‑time health check and repair ---
    OtaSlot slots[3];
    scanAllOtaPartitions(slots);
    const esp_partition_t* current = esp_ota_get_running_partition();
    repairAllInvalidPartitions(slots, current);

    // --- NEW: Check if the CURRENT running partition itself is invalid ---
    // Find the index of the current partition in the slots array
    int current_index = -1;
    for (int i = 0; i < 3; i++) {
        if (slots[i].partition == current) {
            current_index = i;
            break;
        }
    }

    // If current is invalid (and we didn't repair it because we can't write to running partition)
    if (current_index != -1 && !slots[current_index].valid) {
        Serial.println("[ERROR]     FATAL: Currently running partition is INVALID!");
        const esp_partition_t* next = getNextValidPartition(slots);
        if (next) {
            Serial.printf("[INFO]       Switching to valid partition: %s\n", next->label);
            if (esp_ota_set_boot_partition(next) == ESP_OK) {
                Serial.println("[INFO]      Boot partition updated. Restarting now...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            } else {
                Serial.println("[ERROR]     Failed to set boot partition!");
            }
        } else {
            Serial.println("[FATAL]     No valid partition found! Halting.");
            while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
        }
    }

    // --- Main work loop (only reaches here if current is valid) ---
    while (true) {
        // Feed watchdog before any long operation or delay
        esp_task_wdt_reset();

        Serial.println("<Normal Task>       This print identifies this firmware_task is active with task scheduler.");
        vTaskDelay(pdMS_TO_TICKS(1000));

        static unsigned long last_check = 0;
        unsigned long now = millis();
        
        if (millis() - last_check > TASK_PERIOD_MS) { // every 5 Seconds
            last_check = millis();
            scanAllOtaPartitions(slots);
            repairAllInvalidPartitions(slots, current);
            
            // Note: we do not check current validity again because it would require
            // a runtime flash corruption of the executing image – extremely rare.
        }
    }
}




// ------------------------------------------------------------------
// BASE OPERATION
// ------------------------------------------------------------------
/*
2 layer TMR system
- first layer holds 9 copies
- second layer holds 3 copies
- methods include write, read
- write method will save value as a var object that will be stored in 9 copies.
- read method will read value from 9 copy do 1st layer TMR, get 3 winners.
- then do another layer of TMR on them to finalize the actual value and return it.
*/
class var {
public:
    // private:
    // 3×3 storage array (9 copies)
    float copies[3][3];

    // Helper: majority vote among three floats
    float majority(float a, float b, float c) {
        // Count occurrences
        if (a == b) return a;
        if (a == c) return a;
        if (b == c) return b;
        // No majority – fallback to median (or average)
        // Using median avoids extreme outliers
        float arr[] = {a, b, c};
        for (int i = 0; i < 2; i++) {
            for (int j = i+1; j < 3; j++) {
                if (arr[i] > arr[j]) {
                    float t = arr[i]; arr[i] = arr[j]; arr[j] = t;
                }
            }
        }
        return arr[1]; // median
    }

// public:
    // Write a value to all 9 copies
    void write(float value) {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                copies[i][j] = value;
            }
        }
    }

    // Read value with two‑layer TMR
    float read() {
        // Layer 1: majority per row (3 rows)
        float row_majority[3];
        for (int i = 0; i < 3; i++) {
            row_majority[i] = majority(copies[i][0], copies[i][1], copies[i][2]);
        }
        // Layer 2: majority of the three row results
        return majority(row_majority[0], row_majority[1], row_majority[2]);
    }
};

class math {
public:
    static float add(float a, float b) {
        float res[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                volatile float tmp = a + b;   // volatile prevents constant folding
                res[i][j] = tmp;
                asm volatile("" ::: "memory"); // compiler barrier
            }
        }
        return twoStageTMR(res);
    }

    static float sub(float a, float b) {
        float res[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                volatile float tmp = a - b;
                res[i][j] = tmp;
                asm volatile("" ::: "memory");
            }
        }
        return twoStageTMR(res);
    }

    static float mul(float a, float b) {
        float res[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                volatile float tmp = a * b;
                res[i][j] = tmp;
                asm volatile("" ::: "memory");
            }
        }
        return twoStageTMR(res);
    }

    static float div(float a, float b) {
        // Avoid division by zero – return NaN if divisor is 0
        if (b == 0.0f) return NAN;
        float res[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                volatile float tmp = a / b;
                res[i][j] = tmp;
                asm volatile("" ::: "memory");
            }
        }
        return twoStageTMR(res);
    }

private:
    // Two‑stage TMR voter – identical to the logic in var::read()
    static float twoStageTMR(float copies[3][3]) {
        float row_majority[3];
        for (int i = 0; i < 3; i++) {
            row_majority[i] = majority(copies[i][0], copies[i][1], copies[i][2]);
        }
        return majority(row_majority[0], row_majority[1], row_majority[2]);
    }

    static float majority(float a, float b, float c) {
        if (a == b) return a;
        if (a == c) return a;
        if (b == c) return b;
        // No exact match – fallback to median (protects against one outlier)
        float arr[3] = {a, b, c};
        for (int i = 0; i < 2; i++) {
            for (int j = i+1; j < 3; j++) {
                if (arr[i] > arr[j]) {
                    float t = arr[i]; arr[i] = arr[j]; arr[j] = t;
                }
            }
        }
        return arr[1]; // median
    }
};


/*
This is an example task setup for normal firmware tasks. the classes, Methods described 
above are under development and shall encompass critical feature usages and their 
transient redundancy and fault tolerance for time variant curruptions.
*/
void baseOperation(void* pvParameters){
    float reading_from_sensor_01 = 12.3423432;
    float reading_from_sensor_02 = 19.3247923;
    float reading_from_sensor_03 = 32.4537473;
    float reading_from_sensor_04 = 43.4342354;
    float reading_from_sensor_05 = 12.2134434;
    float reading_from_sensor_06 = 54.4353457;

    var sensor_reading_1, sensor_reading_2, sensor_reading_3;
    var sensor_reading_4, sensor_reading_5, sensor_reading_6;

    


    while(true){
        // write values into TMR storage
        sensor_reading_1.write(reading_from_sensor_01);
        sensor_reading_2.write(reading_from_sensor_02);
        sensor_reading_3.write(reading_from_sensor_03);
        sensor_reading_4.write(reading_from_sensor_04);
        sensor_reading_5.write(reading_from_sensor_05);
        sensor_reading_6.write(reading_from_sensor_06);

        // Periodically read and print the values (demonestration)
        float v1 = sensor_reading_1.read();
        float v2 = sensor_reading_2.read();
        float v3 = sensor_reading_3.read();
        float v4 = sensor_reading_4.read();
        float v5 = sensor_reading_5.read();
        float v6 = sensor_reading_6.read();
        
        Serial.println("\n");
        Serial.println("<test>  VARIABLES OPERATIONS TEST  </test>");
        Serial.printf("[TMR] Sensors: %.4f, %.4f, %.4f, %.4f, %.4f, %.4f\n", v1, v2, v3, v4, v5, v6);
        if(reading_from_sensor_01==v1 && reading_from_sensor_02==v2 && reading_from_sensor_03==v3 && reading_from_sensor_04==v4 && reading_from_sensor_05==v5 && reading_from_sensor_06==v6){
            Serial.println("[RESULT]    VALIDATED   [RESULT]");
        }
        Serial.println("\n");

        bool TEST = true;
        if (TEST){
            sensor_reading_1.copies[0][0] = 99.999f;
            sensor_reading_1.copies[1][1] = 29.999f;
        }

        // Math operations TMR
        float sensor_sum    =   math::add(v1, v2);
        float sensor_dif    =   math::sub(v1, v2);
        float product       =   math::mul(v2, v4);
        float ratio         =   math::div(v5, v6);


        Serial.println("\n");
        Serial.println("<test>  MATH OPERATIONS TEST  </test>");
        Serial.printf("[TMR] Sensors: %.4f, %.4f, %.4f, %.4f \n", sensor_sum, sensor_dif, product, ratio);
        Serial.println("\n");

        vTaskDelay(pdMS_TO_TICKS(5000));
    }

}


// ------------------------------------------------------------------
// 6. Setup
// ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n");
    Serial.println("-----------------------------------------------------------------------------");
    Serial.println(" FIRMWARE : ->       void setup running.");
    Serial.println("-----------------------------------------------------------------------------");
    Serial.println("\n\n");

    // Mark this firmware as valid so bootloader doesn't roll back
    esp_ota_mark_app_valid_cancel_rollback();

    // Configure the Task Watchdog
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_MS,
        .idle_core_mask = 0,                                // monitor both cores
        .trigger_panic = true,                              // panic on timeout
    };
    esp_task_wdt_init(&twdt_config);
    
    xTaskCreate(firmwareTask, "firmware_task", 16384, NULL, 5, &firmwareTaskHandle);
    // For demo calculation and reads
    xTaskCreate(baseOperation, "basic_operation", 4096, NULL, 4, &basicOperationHandle);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(100));
}