import shutil
import subprocess
import csv
import time
from pathlib import Path


# ===============================================
#            HELPER FUNCTIONS
# ===============================================

def clear_folder(folder_path, max_retries=3, delay=0.5):
    """Clear all contents of a folder with retry logic for locked files"""
    folder = Path(folder_path)
    
    if folder.exists() and folder.is_dir():
        for item in folder.iterdir():
            for attempt in range(max_retries):
                try:
                    if item.is_dir():
                        shutil.rmtree(item)
                    else:
                        item.unlink()
                    break  # Success, exit retry loop
                except PermissionError as e:
                    if attempt < max_retries - 1:
                        print(f"[WARN]  File locked: {item.name}, retrying in {delay}s... (attempt {attempt + 1}/{max_retries})")
                        time.sleep(delay)
                    else:
                        print(f"[ERROR] Cannot delete {item.name}: {e}")
                except Exception as e:
                    print(f"[ERROR] Error deleting {item.name}: {e}")
                    break
    else:
        print(f"[LOG]    Folder doesn't exist or not in path.")


# ================================================
#    VARIABLES AND CLEARING
# ================================================

## PATHS SETUP
try:
    # Try to get script directory
    ROOT = Path(__file__).parent
except:
    # For Interactive Environments
    ROOT = Path.cwd().parent

# Build folders
BUILD_01 = ROOT / "scripts" / "firmware_01_build"
BUILD_02 = ROOT / "scripts" / "firmware_02_build"
BUILD_03 = ROOT / "scripts" / "firmware_03_build"

# Firmware source folders
FIRMWARE_01 = ROOT / "firmware_01"
FIRMWARE_02 = ROOT / "firmware_02"
FIRMWARE_03 = ROOT / "firmware_03"

# Create directories if they don't exist
BUILD_01.mkdir(parents=True, exist_ok=True)
BUILD_02.mkdir(parents=True, exist_ok=True)
BUILD_03.mkdir(parents=True, exist_ok=True)

FIRMWARE_01.mkdir(parents=True, exist_ok=True)
FIRMWARE_02.mkdir(parents=True, exist_ok=True)
FIRMWARE_03.mkdir(parents=True, exist_ok=True)

# Clear build folders
folders_to_clear = [BUILD_01, BUILD_02, BUILD_03]
print("[INFO]   Clearing build folders")
for f in folders_to_clear:
    clear_folder(f)
print("[INFO]   Done")


# =================================================
#      FIRMWARE COMPILATION
# =================================================
ARDUINO_CLI = "arduino-cli.exe"
PART = ROOT / "scripts" / "partitions.csv"
FQBN = "esp32:esp32:esp32"

compile_jobs = [
    (FIRMWARE_01, BUILD_01),
    (FIRMWARE_02, BUILD_02),
    (FIRMWARE_03, BUILD_03),
]

for fw_dir, build_dir in compile_jobs:
    shutil.copy2(PART, fw_dir / "partitions.csv")
    ino_file = next(fw_dir.glob("*.ino"))
    cmd = [
        ARDUINO_CLI, "compile",
        "-b", FQBN,
        "--build-path", str(build_dir),
        "--build-property", "build.partitions=custom",
        "--build-property", f"build.flash_partitions={PART}",
        "--verbose",
        "--no-color",
        str(ino_file),
    ]
    print(f"[INFO]  Compiling {ino_file.name} ...")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"[ERROR]     {ino_file.name} compilation failed:")
            print(result.stdout)
            print(result.stderr)
        else:
            print(f"[INFO]      {ino_file.name} Compiled OK")
    except Exception as e:
        print(f"[ERROR]     {e}")


# =================================================
#             FLASHING
# =================================================
ESPTOOL = Path(r"C:\Users\Administrator\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.2.0\esptool.exe")
PORT = "COM3"
BAUD = "115200"

# Locate required binaries
PART_BIN = BUILD_01 / "firmware_01.ino.partitions.bin"
boot_app0_matches = list(Path(r"C:\Users\Administrator\AppData\Local\Arduino15\packages\esp32\hardware\esp32").glob(r"*\tools\partitions\boot_app0.bin"))
assert boot_app0_matches, "[ERROR]  boot_app0.bin not found"
BOOT_APP0 = boot_app0_matches[0]

assert ESPTOOL.exists(), f"[ERROR]  esptool not found at {ESPTOOL}"
assert PART_BIN.exists(), f"[ERROR]  partitions.bin not found at {PART_BIN}"
print(f"[INFO]   boot_app0.bin : {BOOT_APP0}")
print(f"[INFO]   partitions.bin: {PART_BIN}")

# Read OTA slot offsets from partitions.csv
offsets = {}
with open(ROOT / "scripts" / "partitions.csv", newline="") as f:
    for row in csv.reader(f):
        row = [c.strip() for c in row]
        if not row or row[0].startswith("#") or row[0] == "Name":
            continue
        name, _, _, offset, *_ = row
        if name.startswith("ota_"):
            offsets[name] = offset

print("[INFO]   Parsed offsets:", offsets)

# Map slot → original binary (no CRC modification)
flash_map = [
    (offsets["ota_0"], BUILD_01 / "firmware_01.ino.bin"),
    (offsets["ota_1"], BUILD_02 / "firmware_02.ino.bin"),
    (offsets["ota_2"], BUILD_03 / "firmware_03.ino.bin"),
]

# Verify all binary files exist before flashing
for offset, bin_path in flash_map:
    assert bin_path.exists(), f"[ERROR]  Binary not found: {bin_path}"
    print(f"[INFO]   Found: {bin_path.name}")

flash_args = []
for offset, bin_path in flash_map:
    flash_args += [offset, str(bin_path)]

cmd = [
    str(ESPTOOL),
    "--chip", "esp32",
    "--port", PORT,
    "--baud", BAUD,
    "write_flash",
    "0x8000", str(PART_BIN),
    "0xe000", str(BOOT_APP0),
    *flash_args,
]

print("\n[INFO]   Flashing...")
print(f"[INFO]   Command: {' '.join(cmd)}")
result = subprocess.run(cmd, capture_output=True, text=True)
print(result.stdout)
if result.returncode != 0:
    print(f"[ERROR]  Flash failed:\n{result.stderr}")
else:
    print("[INFO]   Flash complete successfully!")