=== ESP32 Triple OTA + TMR Test Suite (Manual Reset) ===
[INFO] esptool: C:\Users\Administrator\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe
[INFO] boot_app0.bin: C:\Users\Administrator\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.8\tools\partitions\boot_app0.bin
[INFO] OTA offsets: {'ota_0': '0x10000', 'ota_1': '0x150000', 'ota_2': '0x290000'}
[INFO] Using existing binaries

[INFO] Flashing...
[INFO] Command: C:\Users\Administrator\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe --chip esp32 --port COM3 --baud 115200 --before default_reset --after hard_reset write_flash 0x8000 c:\Users\Administrator\Desktop\code\tmr-esp32\scripts\firmware_01_build\firmware_01.ino.partitions.bin 0xe000 C:\Users\Administrator\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.8\tools\partitions\boot_app0.bin 0x10000 c:\Users\Administrator\Desktop\code\tmr-esp32\scripts\firmware_01_build\firmware_01.ino.bin 0x150000 c:\Users\Administrator\Desktop\code\tmr-esp32\scripts\firmware_02_build\firmware_02.ino.bin 0x290000 c:\Users\Administrator\Desktop\code\tmr-esp32\scripts\firmware_03_build\firmware_03.ino.bin
>>> Press ENTER to start (hold BOOT + press EN if needed)...
[OK] Flash complete
>>> Press ENTER after manually pressing the EN/RST button to boot the firmware...

=== TEST: Normal Operation ===
[MON] ets Jul 29 2019 12:21:46
[MON] rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
[MON] configsip: 0, SPIWP:0xee
[MON] clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
[MON] mode:DIO, clock div:1
[MON] load:0x3fff0030,len:4640
[MON] load:0x40078000,len:15620
[MON] ho 0 tail 12 room 4
[MON] load:0x40080400,len:3164
[MON] entry 0x4008059c
[MON] E (137) esp_core_dump_x+͡: No core dump partition found!
[MON] E (138) esp_core_dump_flash: No
[MON] -----------------------------------------------------------------------------
[MON] FIRMWARE : ->       void setup running.
[MON] -----------------------------------------------------------------------------
[MON] E (1018) task_wdt: esp_task_wdt_init(517): TWDT already initialized
[MON] -----------------------------------------------------------------------------
[MON] Running Firmware Control Task:
[MON] -----------------------------------------------------------------------------
[MON] [INFO]       Scanning all partitions.
[MON] [INFO]       ota_0 is VALID
[MON] [INFO]       ota_1 is VALID
[MON] [INFO]       ota_2 is VALID
[MON] [INFO]       Using ota_0 as source for repairs
[MON] <Normal Task>       This print identifies this firmware_task is active with task scheduler.
[MON] <test>  VARIABLES OPERATIONS TEST  </test>
[MON] [TMR] Sensors: 12.3423, 19.3248, 32.4537, 43.4342, 12.2134, 54.4353
[MON] [RESULT]    VALIDATED   [RESULT]
[MON] <test>  MATH OPERATIONS TEST  </test>
[MON] [TMR] Sensors: 31.6671, -6.9824, 839.3576, 0.2244
[PASS] Normal operation OK

=== TEST: Repair Non‑Running Partition ===

[TEST] Prepare to corrupt ota_0 (offset 0x10000)
>>> MANUAL STEP: Press and HOLD the BOOT button.
>>> While holding BOOT, press and RELEASE the EN/RST button.
>>> Keep holding BOOT until you see 'Connecting...' in the output.
>>> Press ENTER to continue...
[OK] Corrupted ota_0
>>> Press ENTER after pressing the EN/RST button (without BOOT) to reboot the board...
[MON] ets Jul 29 2019 12:21:46
[MON] rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
[MON] configsip: 0, SPIWP:0xee
[MON] clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
[MON] mode:DIO, clock div:1
[MON] load:0x3fff0030,len:4640
[MON] load:0x40078000,len:15620
[MON] ho 0 tail 12 room 4
[MON] load:0x40080400,len:3164
[MON] entry 0x4008059c
[MON] E (137) esp_core_dump_x+͡: No core dump partition found!
[MON] E (138) esp_core_dump_flash: No
[MON] -----------------------------------------------------------------------------
[MON] FIRMWARE : ->       void setup running.
[MON] -----------------------------------------------------------------------------
[MON] E (1018) task_wdt: esp_task_wdt_init(517): TWDT already initialized
[MON] -----------------------------------------------------------------------------
[MON] Running Firmware Control Task:
[MON] -----------------------------------------------------------------------------
[MON] [INFO]       Scanning all partitions.
[MON] [ERROR]      ota_0 is INVALID (err: ESP_ERR_IMAGE_INVALID)
[MON] [INFO]       ota_1 is VALID
[MON] [INFO]       ota_2 is VALID
[MON] [INFO]       Using ota_2 as source for repairs
[MON] [INFO]       Attempting to repair invalid partition: ota_0
[MON] [INFO]       Repairing ota_0 <- ota_2 (size: 1310720 bytes)
[MON] <test>  VARIABLES OPERATIONS TEST  </test>
[MON] [TMR] Sensors: 12.3423, 19.3248, 32.4537, 43.4342, 12.2134, 54.4353
[MON] [RESULT]    VALIDATED   [RESULT]
[MON] <test>  MATH OPERATIONS TEST  </test>
[MON] [TMR] Sensors: 31.6671, -6.9824, 839.3576, 0.2244
[MON] [INFO]       Repair successful, ota_0 is now VALID
[PASS] Repair test OK

=== TEST: Corrupt a Partition (may be running or not) ===

[TEST] Prepare to corrupt ota_0 (offset 0x10000)
>>> MANUAL STEP: Press and HOLD the BOOT button.
>>> While holding BOOT, press and RELEASE the EN/RST button.
>>> Keep holding BOOT until you see 'Connecting...' in the output.
>>> Press ENTER to continue...
[OK] Corrupted ota_0
>>> Press ENTER after pressing the EN/RST button (without BOOT) to reboot the board...
[MON] ets Jul 29 2019 12:21:46
[MON] rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
[MON] configsip: 0, SPIWP:0xee
[MON] clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
[MON] mode:DIO, clock div:1
[MON] load:0x3fff0030,len:4640
[MON] load:0x40078000,len:15620
[MON] ho 0 tail 12 room 4
[MON] load:0x40080400,len:3164
[MON] entry 0x4008059c
[MON] E (137) esp_core_dump_x+͡: No core dump partition found!
[MON] E (138) esp_core_dump_flash: o
[MON] -----------------------------------------------------------------------------
[MON] FIRMWARE : ->       void setup running.
[MON] -----------------------------------------------------------------------------
[MON] E (1018) task_wdt: esp_task_wdt_init(517): TWDT already initialized
[MON] -----------------------------------------------------------------------------
[MON] Running Firmware Control Task:
[MON] -----------------------------------------------------------------------------
[MON] [INFO]       Scanning all partitions.
[MON] [ERROR]      ota_0 is INVALID (err: ESP_ERR_IMAGE_INVALID)
[MON] [INFO]       ota_1 is VALID
[MON] [INFO]       ota_2 is VALID
[MON] [INFO]       Using ota_2 as source for repairs
[MON] [INFO]       Attempting to repair invalid partition: ota_0
[MON] [INFO]       Repairing ota_0 <- ota_2 (size: 1310720 bytes)
[MON] <test>  VARIABLES OPERATIONS TEST  </test>
[MON] [TMR] Sensors: 12.3423, 19.3248, 32.4537, 43.4342, 12.2134, 54.4353
[MON] [RESULT]    VALIDATED   [RESULT]
[MON] <test>  MATH OPERATIONS TEST  </test>
[MON] [TMR] Sensors: 31.6671, -6.9824, 839.3576, 0.2244
[MON] [INFO]       Repair successful, ota_0 is now VALID
[PASS] Corrupted partition was repaired

=== TEST SUMMARY ===
normal               : PASS
repair_non_running   : PASS
corrupt_running      : PASS

[SUCCESS] All tests passed!