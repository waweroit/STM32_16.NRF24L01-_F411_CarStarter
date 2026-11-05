"""Reproducible ARM build independent of CubeIDE's generated makefiles.
Run: python tools/build.py [--debug] [--gcc path/to/arm-none-eabi-gcc.exe]
The script builds every source under Core, Drivers, Middlewares and USB_DEVICE.
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def compiler():
    found = shutil.which("arm-none-eabi-gcc")
    if found:
        return found
    candidates = sorted(Path("C:/ST").glob("**/tools/bin/arm-none-eabi-gcc.exe"))
    if not candidates:
        raise SystemExit("Set --gcc to the STM32 ARM GCC executable")
    return str(candidates[-1])

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gcc")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    gcc = args.gcc or compiler()
    out = ROOT / "build" / ("debug" if args.debug else "release")
    out.mkdir(parents=True, exist_ok=True)
    includes = ["Core/Inc", "USB_DEVICE/App", "USB_DEVICE/Target",
        "Drivers/STM32F4xx_HAL_Driver/Inc", "Drivers/STM32F4xx_HAL_Driver/Inc/Legacy",
        "Drivers/CMSIS/Include", "Drivers/CMSIS/Device/ST/STM32F4xx/Include",
        "Middlewares/ST/STM32_USB_Device_Library/Core/Inc",
        "Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc"]
    common = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard",
        "-DSTM32F411xE", "-DUSE_HAL_DRIVER", "-ffunction-sections", "-fdata-sections",
        "-g3", "-O0" if args.debug else "-Os"]
    if args.debug:
        common.append("-DDEBUG")
    inc = ["-I" + str(ROOT / i) for i in includes]
    sources = []
    for folder in ("Core", "Drivers", "Middlewares", "USB_DEVICE"):
        sources += [p for p in (ROOT/folder).rglob("*.c") if "_template" not in p.name]
    sources += list((ROOT/"Core/Startup").glob("*.s"))
    def compile_one(src):
        obj = out / (str(src.relative_to(ROOT)).replace("\\", "_").replace("/", "_") + ".o")
        command = [gcc, *common, *inc, "-c", str(src), "-o", str(obj)]
        if src.suffix == ".c":
            command += ["-std=gnu11", "-Wall", "-Wextra", "-Werror=implicit-function-declaration"]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.stdout or result.stderr:
            print(str(src.relative_to(ROOT)) + "\n" + result.stdout + result.stderr, end="")
        return obj, result.returncode
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 2, 8)) as pool:
        results = list(pool.map(compile_one, sources))
    if any(code for _, code in results):
        raise SystemExit("Compilation failed")
    elf = out/"CarStarter.elf"
    response = out/"objects.rsp"
    response.write_text("\n".join('"'+str(obj).replace('\\','/')+'"' for obj,_ in results))
    subprocess.run([gcc, *common, "@"+str(response), "-T"+str(ROOT/"STM32F411CEUX_FLASH.ld"),
        "--specs=nano.specs", "--specs=nosys.specs", "-Wl,--gc-sections", "-Wl,--print-memory-usage",
        "-Wl,-Map="+str(out/"CarStarter.map"), "-o", str(elf), "-lm"], check=True)
    bin_dir = Path(gcc).parent
    suffix = ".exe" if os.name == "nt" else ""
    subprocess.run([str(bin_dir/("arm-none-eabi-objcopy"+suffix)), "-O", "ihex", str(elf), str(out/"CarStarter.hex")], check=True)
    subprocess.run([str(bin_dir/("arm-none-eabi-size"+suffix)), str(elf)], check=True)
    print("Built", elf)

if __name__ == "__main__":
    main()
