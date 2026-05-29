#line 1 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#define TASK_PERIOD_MS      60000   //  Rechecking time gap for currpuption and repair in firmware
#define WATCHDOG_TIMEOUT_MS 30000   //  Having margin over the max time the repair could take
#define COPY_CHUNK_SIZE     4096    //  chunk sizes for partition copy and repair

// task handles
TaskHandle_t firmwareTaskHandle     =   NULL;
TaskHandle_t basicOperationHandle   =   NULL;


/*
## Firmware management system
These functions maintains operating firmware and their reparing.
*/
struct OtaSlot {
    const esp_partition_t* partition;
    bool valid;
    const char* label;
};

// ------------------------------------------------------------------
// 1. Scan all three OTA partitions using esp_partition_get_sha256()
// ------------------------------------------------------------------
#line 31 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
bool scanAllOtaPartitions(OtaSlot slots[3]);
#line 67 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
bool repairPartition(const esp_partition_t* dst, const esp_partition_t* src);
#line 149 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
void repairAllInvalidPartitions(OtaSlot slots[3], const esp_partition_t* current);
#line 183 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
const esp_partition_t* getNextValidPartition(OtaSlot slots[3]);
#line 222 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
void firmwareTask(void* pvParameters);
#line 483 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
void baseOperation(void* pvParameters);
#line 543 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
void setup();
#line 569 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
void loop();
#line 31 "C:\\Users\\Administrator\\Desktop\\code\\tmr-esp32\\firmware_01\\firmware_01.ino"
bool scanAllOtaPartitions(OtaSlot slots[3]) {
    Serial.println("[INFO]       Scanning all partitions.");
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
    esp_task_wdt_reset();
    esp_err_t err = esp_partition_erase_range(dst, 0, dst->size);
    esp_task_wdt_reset();
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
private:
    // 3×3 storage = 9 independent copies
    float copies[3][3];

    // Helper: majority vote on three floats (with tolerance & NaN/Inf handling)
    static float majority(float a, float b, float c, float epsilon = 1e-6f) {
        // Count NaNs
        bool a_nan = isnan(a), b_nan = isnan(b), c_nan = isnan(c);
        int nan_cnt = a_nan + b_nan + c_nan;
        if (nan_cnt >= 2) return NAN;

        // Handle infinities
        if (isinf(a) || isinf(b) || isinf(c)) {
            int pos_inf = (a == INFINITY) + (b == INFINITY) + (c == INFINITY);
            if (pos_inf >= 2) return INFINITY;
            int neg_inf = (a == -INFINITY) + (b == -INFINITY) + (c == -INFINITY);
            if (neg_inf >= 2) return -INFINITY;
        }

        // Equality with tolerance (for rounding differences)
        auto approx_equal = [epsilon](float x, float y) {
            return fabsf(x - y) <= epsilon * fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y)));
        };

        if (approx_equal(a, b)) return a;
        if (approx_equal(a, c)) return a;
        if (approx_equal(b, c)) return b;

        // Fallback to median
        float arr[] = {a, b, c};
        for (int i = 0; i < 2; ++i) {
            for (int j = i+1; j < 3; ++j) {
                if (isnan(arr[j])) continue;
                if (isnan(arr[i]) || arr[i] > arr[j]) {
                    float t = arr[i]; arr[i] = arr[j]; arr[j] = t;
                }
            }
        }
        return arr[1];
    }

    // Repair a single copy by reading its row and column neighbours
    void repairCopy(int row, int col) {
        // Get majority from the row (excluding this column) and column (excluding this row)
        float row_maj = majority(
            copies[row][0], copies[row][1], copies[row][2]
        );
        float col_maj = majority(
            copies[0][col], copies[1][col], copies[2][col]
        );
        // Replace with the majority of the two majorities and the old value
        copies[row][col] = majority(row_maj, col_maj, copies[row][col]);
    }

public:
    // Write with read‑back verification (mitigates write‑time corruption)
    void write(float value) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                copies[i][j] = value;
                // Read back and compare (within tolerance)
                float readback = copies[i][j];
                if (fabsf(readback - value) > 1e-6f) {
                    // Write failed – retry once
                    copies[i][j] = value;
                }
            }
        }
    }

    // Read with two‑layer TMR + optional auto‑repair
    float read(bool autoRepair = true) {
        float row_maj[3];
        for (int i = 0; i < 3; ++i) {
            row_maj[i] = majority(copies[i][0], copies[i][1], copies[i][2]);
        }
        float final_val = majority(row_maj[0], row_maj[1], row_maj[2]);

        // If auto‑repair is on and a copy mismatched, fix it
        if (autoRepair) {
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    if (fabsf(copies[i][j] - final_val) > 1e-6f) {
                        repairCopy(i, j);
                    }
                }
            }
        }
        return final_val;
    }

    // Scrubbing: periodically read all copies and force repair
    void scrub() {
        (void)read(true);  // read with auto‑repair will fix everything
    }
};


class math {
private:
    // Perform one arithmetic operation 9 times and vote
    template<typename Op>
    static float redundantOperation(Op operation) {
        float results[3][3];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                volatile float tmp = operation();  // each call is independent
                results[i][j] = tmp;
                asm volatile("" ::: "memory");     // compiler barrier
            }
        }
        return twoStageTMR(results);
    }

    static float twoStageTMR(float copies[3][3]) {
        float row_maj[3];
        for (int i = 0; i < 3; ++i) {
            row_maj[i] = majority(copies[i][0], copies[i][1], copies[i][2]);
        }
        return majority(row_maj[0], row_maj[1], row_maj[2]);
    }

    static float majority(float a, float b, float c, float eps = 1e-6f) {
        // Same robust majority as in var class
        bool a_nan = isnan(a), b_nan = isnan(b), c_nan = isnan(c);
        int nan_cnt = a_nan + b_nan + c_nan;
        if (nan_cnt >= 2) return NAN;

        if (isinf(a) || isinf(b) || isinf(c)) {
            int pos_inf = (a == INFINITY) + (b == INFINITY) + (c == INFINITY);
            if (pos_inf >= 2) return INFINITY;
            int neg_inf = (a == -INFINITY) + (b == -INFINITY) + (c == -INFINITY);
            if (neg_inf >= 2) return -INFINITY;
        }

        auto approx_equal = [eps](float x, float y) {
            return fabsf(x - y) <= eps * fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y)));
        };

        if (approx_equal(a, b)) return a;
        if (approx_equal(a, c)) return a;
        if (approx_equal(b, c)) return b;

        float arr[] = {a, b, c};
        for (int i = 0; i < 2; ++i) {
            for (int j = i+1; j < 3; ++j) {
                if (isnan(arr[j])) continue;
                if (isnan(arr[i]) || arr[i] > arr[j]) {
                    float t = arr[i]; arr[i] = arr[j]; arr[j] = t;
                }
            }
        }
        return arr[1];
    }

public:
    static float add(float a, float b) {
        return redundantOperation([&]() { return a + b; });
    }

    static float sub(float a, float b) {
        return redundantOperation([&]() { return a - b; });
    }

    static float mul(float a, float b) {
        return redundantOperation([&]() { return a * b; });
    }

    static float div(float a, float b) {
        if (b == 0.0f) return NAN;
        return redundantOperation([&]() { return a / b; });
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
        .idle_core_mask = 0,                                // leaves idle tasks
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
