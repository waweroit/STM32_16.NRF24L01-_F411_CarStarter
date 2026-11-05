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
sources = ["tests/test_core.c", "Core/Src/CarEngine.c", "Core/Src/CarStorage.c",
           "Core/Src/SecureProtocol.c", "Core/Src/DeviceKeys.c", "Core/Src/Crypto.c", "Core/Src/tiny_aes.c"]
subprocess.run([compiler(), "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=soft", "-Os", "-g",
    "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
    "-I"+str(ROOT/"Core/Inc"), "-include", str(ROOT/"tests/test_port.h"),
    *[str(ROOT/s) for s in sources], "-T"+str(ROOT/"tests/test.ld"),
    "-nostartfiles", "--specs=nosys.specs", "-Wl,--gc-sections", "-o", str(elf)], check=True)
uc=Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.mem_map(0x10000,0x90000)
uc.mem_map(0x20000000,128*1024)
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
if result or group!=4:
    raise SystemExit(f"FAIL stage={group}, tests/test_core.c line={result}")
print("PASS: engine sequencing/timers/rollover; NOR journal rotations + 128 power cuts; authenticated frames/replay/reset.")

