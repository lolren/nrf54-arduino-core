"""
PLATFORM build script (not framework-specific). Invoked directly by
PlatformIO as the platform's build entry point -- get_build_script() finds
this exact path. Sets up the toolchain and generic compile/link flags, then
hands off to env.BuildProgram(), which internally loads the SEPARATE
framework script (builder/frameworks/arduino.py) before actual compilation.

Do NOT reference this same file from platform.json's frameworks.arduino.script
-- that caused infinite recursion (BuildProgram -> BuildFrameworks -> this
file again -> BuildProgram again...). Platform script and framework script
must be different files; see builder/frameworks/arduino.py for the
Arduino-core-specific setup (include paths, defines, SDC lib linking).
"""

from os.path import join
from SCons.Script import DefaultEnvironment, AlwaysBuild, Default

env = DefaultEnvironment()

env.Replace(
    AR="arm-none-eabi-ar",
    AS="arm-none-eabi-as",
    CC="arm-none-eabi-gcc",
    CXX="arm-none-eabi-g++",
    GDB="arm-none-eabi-gdb",
    OBJCOPY="arm-none-eabi-objcopy",
    RANLIB="arm-none-eabi-ranlib",
    SIZETOOL="arm-none-eabi-size",
    ARFLAGS=["rcs"],   # compiler.ar.flags

    # Shared across C/C++/S per compiler.c.flags / compiler.cpp.flags /
    # compiler.S.flags -- mcu/thumb/float-abi identical across all three.
    CCFLAGS=[
        "-g", "-Os",
        "-ffunction-sections", "-fdata-sections",
        "-MMD",
        "-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=soft",  # NOTE: no -mfpu flag -- confirmed, not an omission
        "-Wall",
    ],

    CFLAGS=["-std=gnu11"],

    CXXFLAGS=[
        "-std=gnu++17",
        "-fpermissive", "-fno-exceptions", "-fno-rtti",
        "-fno-threadsafe-statics", "-fno-use-cxa-atexit", "-fno-sized-deallocation",
    ],

    ASFLAGS=["-x", "assembler-with-cpp"],

    # compiler.c.elf.flags + compiler.ldflags, concatenated. Note: -T
    # (linker script) is NOT set here -- it depends on the core/board dir,
    # which is framework-specific info this platform script doesn't have.
    # Set in builder/frameworks/arduino.py instead.
    LINKFLAGS=[
        "-Wl,--gc-sections",
        "-Wl,-Map,${BUILD_DIR}/${PROGNAME}.map",
        "-mcpu=cortex-m33", "-mthumb", "-mfloat-abi=soft",
        "--specs=nano.specs", "--specs=nosys.specs",
    ],

    LIBS=["m"],
)

# --- Build + register PlatformIO's standard SCons targets ---
target_elf = env.BuildProgram()
# .hex generation via plain SCons env.Command(), not env.ElfToHex() --
# that PlatformIO helper turned out not to exist in this setup (verified
# by the actual AttributeError, not assumed away). This does exactly what
# platform.txt's recipe.objcopy.hex.pattern does: arm-none-eabi-objcopy
# -O ihex elf hex. No dependency on any PlatformIO-internal helper existing.
target_hex = env.Command(
    join("$BUILD_DIR", "${PROGNAME}.hex"),
    target_elf,
    env.VerboseAction(
        '"$OBJCOPY" -O ihex $SOURCE $TARGET',
        "Building $TARGET"
    )
)

AlwaysBuild(env.Alias("nobuild", target_hex))
target_buildprog = env.Alias("buildprog", target_hex, target_hex)

target_size = env.Alias(
    "size", target_elf,
    env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE")
)
AlwaysBuild(target_size)

AlwaysBuild(env.Alias(
    "checkprogsize", target_elf,
    env.VerboseAction(env.CheckUploadSize, "Checking size $SOURCE")
))

Default(target_buildprog)

# --- Upload: real pyOCD wiring, not just a manual-command note anymore.
# Confirmed manually against hardware first (pyocd flash -t nrf54l
# --probe <id> ...) before wiring it into `pio run -t upload` here, so any
# remaining problem is isolated to this SCons plumbing rather than pyOCD
# itself or the hex file.
#
# Target string is read from board.json's debug.tools.pyocd.server.arguments
# rather than hardcoded here again -- that value is the one we independently
# confirmed against real hardware (`pyocd list --targets`), and pulling it
# from the board manifest means this keeps working unchanged once other
# board manifests (e.g. the LM20A) exist, without editing this file.
#
# KNOWN LIMITATION: no --probe flag is passed, so pyOCD auto-selects
# whichever single debug probe is attached. Fine for one board connected
# at a time; ambiguous (and will likely error) with multiple probes
# attached simultaneously. Add --probe <id> to UPLOADERFLAGS below if that
# becomes a real problem.
board = env.BoardConfig()
pyocd_target_args = board.get("debug.tools.pyocd.server.arguments", ["-t", "nrf54l"])

env.Replace(
    UPLOADER="pyocd",
    UPLOADERFLAGS=["flash"] + list(pyocd_target_args),
    UPLOADCMD='"$UPLOADER" $UPLOADERFLAGS "$SOURCE"',
)

target_upload = env.Alias(
    "upload", target_hex,
    env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")
)
AlwaysBuild(target_upload)