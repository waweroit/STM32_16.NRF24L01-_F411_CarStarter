"""Execute production ARM C code in Unicorn (no hardware/peripheral tests).
Dependencies: pip install --target build/test-deps unicorn pyelftools
"""
from pathlib import Path
import subprocess
import sys
from build import compiler, ROOT
sys.path.insert(0, str(ROOT/"build/test-deps"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0

out = ROOT/"build/tests"
out.mkdir(parents=True, exist_ok=True)
elf = out/"tests.elf"
sources = ["tests/test_core.c", "tests/test_start.c", "tests/test_button.c", "Core/Src/CarButton.c", "tests/test_sensors.c", "tests/test_radio.c",
           "Core/Src/nRF24L01.c", "Core/Src/NrfLink.c", "Core/Src/CarSensors.c",
           "Core/Src/ow.c", "Core/Src/ds18b20.c", "Core/Src/CarEngine.c", "Core/Src/CarStorage.c",
           "Core/Src/SecureProtocol.c", "Core/Src/DeviceKeys.c", "Core/Src/Crypto.c", "Core/Src/tiny_aes.c"]
subprocess.run([compiler(), "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=soft", "-Os", "-g",
    "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
    "-DSTM32F411xE", "-DUSE_HAL_DRIVER",
    *["-I"+str(ROOT/inc) for inc in ("Drivers/STM32F4xx_HAL_Driver/Inc",
      "Drivers/CMSIS/Include", "Drivers/CMSIS/Device/ST/STM32F4xx/Include")],
    "-I"+str(ROOT/"Core/Inc"), "-include", str(ROOT/"tests/test_port.h"),
    *[str(ROOT/s) for s in sources], "-T"+str(ROOT/"tests/test.ld"),
    "-nostartfiles", "--specs=nosys.specs", "-Wl,--gc-sections", "-o", str(elf)], check=True)
uc=Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.mem_map(0x10000,0x90000)
uc.mem_map(0x20000000,128*1024)
uc.mem_map(0x40020000,0x1000)  # GPIOA registers for the simulated 1-Wire bus
with elf.open("rb") as file:
    binary=ELFFile(file)
    for seg in binary.iter_segments():
        if seg["p_type"]=="PT_LOAD" and seg["p_filesz"]:
            uc.mem_write(seg["p_vaddr"],seg.data())
    entry=binary.header["e_entry"]
    stage=binary.get_section_by_name(".symtab").get_symbol_by_name("test_stage")[0]["st_value"]
uc.reg_write(UC_ARM_REG_SP,0x2001fff0)
uc.reg_write(UC_ARM_REG_LR,0x90001)
uc.emu_start(entry | 1,0x90000,count=500_000_000)
result=uc.reg_read(UC_ARM_REG_R0)
group=int.from_bytes(uc.mem_read(stage,4),"little")
if result or group!=8:
    raise SystemExit(f"FAIL stage={group}, test source line={result}")
print("PASS: START request and timer; engine sequencing/timers/rollover; NOR journal rotations + 128 power cuts; authenticated frames/replay/reset; DS18B20 startup, suspend and error recovery; NRF cold initialization and retry; button wake backlog, debounce, long press, overflow and rollover.")

