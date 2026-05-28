#!/usr/bin/env python3
"""
ESP32 Triple OTA + TMR Test Script – Manual Reset Version
"""

import subprocess
import sys
import time
import csv
import shutil
import tempfile
from pathlib import Path
import serial

# ========== CONFIGURATION ==========
ROOT = Path(__file__).parent.parent
ARDUINO_CLI = "arduino-cli"
FQBN = "esp32:esp32:esp32"
PORT = "COM3"
BAUD = 115200

ESPTOOL = None
BOOT_APP0 = None
PARTITIONS_CSV = ROOT / "scripts" / "partitions.csv"
FW_DIRS = [ROOT / f"firmware_{i:02d}" for i in range(1, 4)]
BUILD_DIRS = [ROOT / "scripts" / f"firmware_{i:02d}_build" for i in range(1, 4)]
offsets = {}

SERIAL_TIMEOUT = 45
BOOT_DELAY = 3
REPAIR_WAIT = 60

# ========== HELPER FUNCTIONS ==========
def find_esptool_and_bootapp():
    global ESPTOOL, BOOT_APP0
    arduino15 = Path.home() / "AppData" / "Local" / "Arduino15"
    esptool_candidates = list(arduino15.glob("packages/esp32/tools/esptool_py/*/esptool*"))
    if not esptool_candidates:
        raise FileNotFoundError("esptool not found in Arduino15")
    ESPTOOL = esptool_candidates[0]
    boot_candidates = list(arduino15.glob("packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin"))
    if not boot_candidates:
        raise FileNotFoundError("boot_app0.bin not found")
    BOOT_APP0 = boot_candidates[0]
    print(f"[INFO] esptool: {ESPTOOL}")
    print(f"[INFO] boot_app0.bin: {BOOT_APP0}")

def compile_firmware():
    for fw in FW_DIRS:
        shutil.copy2(PARTITIONS_CSV, fw / "partitions.csv")
        fw.mkdir(parents=True, exist_ok=True)
    for fw_dir, build_dir in zip(FW_DIRS, BUILD_DIRS):
        ino_file = next(fw_dir.glob("*.ino"), None)
        if not ino_file:
            print(f"[ERROR] No .ino file in {fw_dir}")
            continue
        build_dir.mkdir(parents=True, exist_ok=True)
        cmd = [ARDUINO_CLI, "compile", "-b", FQBN, "--build-path", str(build_dir),
               "--build-property", "build.partitions=custom",
               "--build-property", f"build.flash_partitions={PARTITIONS_CSV}",
               "--verbose", "--no-color", str(ino_file)]
        print(f"[INFO] Compiling {ino_file.name} ...")
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print("[ERROR] Compilation failed:", result.stderr)
            sys.exit(1)
        print(f"[OK] {ino_file.name} compiled")

def read_partition_offsets():
    with open(PARTITIONS_CSV, newline="") as f:
        for row in csv.reader(f):
            row = [c.strip() for c in row]
            if not row or row[0].startswith("#") or row[0] == "Name":
                continue
            name, typ, subtype, offset, size = row[:5]
            if name.startswith("ota_"):
                offsets[name] = offset
    print(f"[INFO] OTA offsets: {offsets}")
    assert len(offsets) == 3, "Missing OTA partitions in CSV"

def flash_all():
    part_bin = BUILD_DIRS[0] / "firmware_01.ino.partitions.bin"
    binaries = []
    for i, (name, build_dir) in enumerate(zip(["ota_0", "ota_1", "ota_2"], BUILD_DIRS)):
        bin_path = build_dir / f"firmware_{i+1:02d}.ino.bin"
        if not bin_path.exists():
            print(f"[ERROR] Missing binary: {bin_path}")
            sys.exit(1)
        binaries.append((offsets[name], bin_path))

    cmd = [str(ESPTOOL), '--chip', 'esp32', '--port', PORT, '--baud', '115200',
           '--before', 'default_reset', '--after', 'hard_reset',
           'write_flash', '0x8000', str(part_bin), '0xe000', str(BOOT_APP0)]
    for offset, bin_path in binaries:
        cmd.extend([offset, str(bin_path)])

    print("\n[INFO] Flashing...")
    print(f"[INFO] Command: {' '.join(cmd)}")
    input(">>> Press ENTER to start (hold BOOT + press EN if needed)...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("[ERROR] Flash failed:", result.stderr)
        sys.exit(1)
    print("[OK] Flash complete")
    input(">>> Press ENTER after manually pressing the EN/RST button to boot the firmware...")

def corrupt_partition(partition_name):
    offset_str = offsets.get(partition_name)
    if not offset_str:
        print(f"[ERROR] Unknown partition {partition_name}")
        return False
    offset = int(offset_str, 16)
    garbage = b'\xAA' * (64 * 1024)
    with tempfile.NamedTemporaryFile(delete=False, suffix='.bin') as tmp:
        tmp.write(garbage)
        tmp_name = tmp.name
    cmd = [str(ESPTOOL), '--chip', 'esp32', '--port', PORT, '--baud', '115200',
           '--before', 'default_reset', '--after', 'hard_reset',
           'write_flash', hex(offset), tmp_name]
    print(f"\n[TEST] Prepare to corrupt {partition_name} (offset {offset_str})")
    print(">>> MANUAL STEP: Press and HOLD the BOOT button.")
    print(">>> While holding BOOT, press and RELEASE the EN/RST button.")
    print(">>> Keep holding BOOT until you see 'Connecting...' in the output.")
    input(">>> Press ENTER to continue...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    try:
        Path(tmp_name).unlink()
    except:
        pass
    if result.returncode != 0:
        print(f"[ERROR] Corruption failed:", result.stderr)
        return False
    print(f"[OK] Corrupted {partition_name}")
    input(">>> Press ENTER after pressing the EN/RST button (without BOOT) to reboot the board...")
    return True

def monitor_serial(timeout=SERIAL_TIMEOUT, expect_strings=None):
    if expect_strings is None:
        expect_strings = []
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        time.sleep(BOOT_DELAY)
        found = set()
        start = time.time()
        while time.time() - start < timeout:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[MON] {line}")
                for exp in expect_strings:
                    if exp in line:
                        found.add(exp)
                if len(found) == len(expect_strings):
                    ser.close()
                    return True
        ser.close()
        if found:
            print(f"[WARN] Only found {found}, missing {set(expect_strings)-found}")
        else:
            print("[ERROR] No expected output received")
        return False
    except Exception as e:
        print(f"[ERROR] Serial monitor: {e}")
        return False

def test_normal_operation():
    print("\n=== TEST: Normal Operation ===")
    expected = ["[RESULT]    VALIDATED",
                "[TMR] Sensors: 31.6671, -6.9824, 839.3576, 0.2244"]
    success = monitor_serial(timeout=45, expect_strings=expected)
    print("[PASS] Normal operation OK" if success else "[FAIL] Normal operation check failed")
    return success

def test_repair_non_running():
    print("\n=== TEST: Repair Non‑Running Partition ===")
    if not corrupt_partition("ota_0"):
        return False
    expected = ["[ERROR]      ota_0 is INVALID",
                "Attempting to repair invalid partition: ota_0",
                "Repair successful, ota_0 is now VALID"]
    success = monitor_serial(timeout=REPAIR_WAIT, expect_strings=expected)
    print("[PASS] Repair test OK" if success else "[FAIL] Repair test failed")
    return success

def test_corrupt_running():
    print("\n=== TEST: Corrupt a Partition (may be running or not) ===")
    running = "ota_0"  # or detect from serial
    if not corrupt_partition(running):
        return False
    # After reset, we expect the firmware to either:
    #   - repair the corrupted partition (if it is not running)
    #   - or boot from another valid partition and still repair it
    expected = [
        "ota_0 is INVALID",
        "Repair successful, ota_0 is now VALID"
    ]
    success = monitor_serial(timeout=REPAIR_WAIT, expect_strings=expected)
    print("[PASS] Corrupted partition was repaired" if success else "[FAIL]")
    return success

def main():
    print("=== ESP32 Triple OTA + TMR Test Suite (Manual Reset) ===")
    find_esptool_and_bootapp()
    read_partition_offsets()

    if not all((bd / f"firmware_{i:02d}.ino.bin").exists() for i, bd in enumerate(BUILD_DIRS, start=1)):
        print("[INFO] Compiling firmwares...")
        compile_firmware()
    else:
        print("[INFO] Using existing binaries")

    flash_all()

    results = {}
    results["normal"] = test_normal_operation()
    if not results["normal"]:
        print("\n[ABORT] Normal operation failed")
        sys.exit(1)

    results["repair_non_running"] = test_repair_non_running()
    results["corrupt_running"] = test_corrupt_running()

    print("\n=== TEST SUMMARY ===")
    for name, passed in results.items():
        print(f"{name:20} : {'PASS' if passed else 'FAIL'}")
    if all(results.values()):
        print("\n[SUCCESS] All tests passed!")
    else:
        print("\n[FAILURE] Some tests failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()