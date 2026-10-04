"""
FRAMEWORK build script for the nrf54l15 Arduino core. Loaded automatically
by PlatformIO from inside env.BuildProgram() (via BuildFrameworks() ->
GetFrameworkScript() -> SConscript(path, exports="env")) -- NOT invoked
directly. This is a genuinely separate file from the platform's own
builder/main.py; pointing both roles at one file caused infinite recursion.

Import("env") is correct HERE (unlike in the platform script) because this
file is loaded as a sub-script with env explicitly exported into it.

Sets Arduino-core-specific include paths, defines, and the Nordic
SoftDevice Controller static-lib link group.
"""

from os.path import join
from SCons.Script import Import

Import("env")

board = env.BoardConfig()
FRAMEWORK_DIR = env.PioPlatform().get_package_dir("framework-nrf54l15clean")
CORE = board.get("build.core")                 # "nrf54l15"
VARIANT = board.get("build.variant")            # "xiao_nrf54l15"
LDSCRIPT = board.get("build.ldscript")          # "nrf54l15_linker_script.ld"
BOARD_DEFINE = board.get("build.board_define")  # "XIAO_NRF54L15_CLEAN"
ARCH_DEFINE = board.get("build.arch_define")    # "NRF54L15CLEAN"
EXTRA_FLAGS = board.get("build.extra_flags")    # list, see board json

# The git repo uses Arduino's standard "manual install" layout
# (hardware/<vendor>/<architecture>/) rather than the flattened layout
# Boards Manager release tarballs use -- confirmed via a real failed build
# and a real directory listing, not assumed. Everything below this offset
# is otherwise identical in structure to the tarball layout.
FRAMEWORK_SRC_DIR = f"{FRAMEWORK_DIR}/hardware/nrf54l15clean/nrf54l15clean"

CORE_DIR = f"{FRAMEWORK_SRC_DIR}/cores/{CORE}"
VARIANT_DIR = f"{FRAMEWORK_SRC_DIR}/variants/{VARIANT}"
NORDIC_SDC_ARCH = board.get("build.nordic_sdc_arch")  # "nrf54l" for L15, "nrf54lm" for LM20 -- confirmed per-board, was previously hardcoded to L15's value, a real latent bug for any other board
NORDIC_SDC_DIR = f"{FRAMEWORK_SRC_DIR}/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/lib/{NORDIC_SDC_ARCH}"

# Forced includes (compiler.c.extra_flags / compiler.cpp.extra_flags /
# compiler.S.extra_flags in platform.txt). CoreVersionGenerated.h and
# BuildTargetGuard.h are both real, tracked files in the repo -- confirmed
# directly via `git show <commit>:<path>` against the exact commit a
# failing build had checked out. An earlier version of this script
# generated CoreVersionGenerated.h defensively, based on a wrong diagnosis
# (assumed it was release-time-generated and untracked); the real cause of
# that failure was CORE_DIR being wrong (missing the hardware/<vendor>/<arch>
# nesting prefix, fixed above) -- open() in write mode raises the same
# FileNotFoundError when a parent directory doesn't exist, which is what
# actually happened. No generation needed once the path itself is correct.
env.Append(CCFLAGS=[
    "-include", f"{CORE_DIR}/CoreVersionGenerated.h",
    "-include", f"{CORE_DIR}/BuildTargetGuard.h",
])

env.Append(CPPDEFINES=[
    ("F_CPU", "$BOARD_F_CPU"),
    ("ARDUINO", "10819"),   # runtime.ide.version -- arbitrary stand-in
    f"ARDUINO_{BOARD_DEFINE}",
    f"ARDUINO_ARCH_{ARCH_DEFINE}",
])

# extra_flags from board manifest are pre-formed "-Dxxx" strings (mirrors
# boards.txt's build.extra_flags composite exactly).
env.Append(CCFLAGS=EXTRA_FLAGS)

env.Append(
    CPPPATH=[
        "$PROJECT_DIR",
        CORE_DIR,
        VARIANT_DIR,
    ],
    # CORE_DIR added here (not just NORDIC_SDC_DIR) so that ld can resolve
    # the linker script via its standard -L search-path behavior -- see
    # note below on why we're not passing our own full -T path directly.
    LIBPATH=[NORDIC_SDC_DIR, CORE_DIR],
)

# Linker script. NOT setting "-T" + full path manually here anymore --
# that produced a linker error even though the printed path was fully
# correct and non-empty. Best explanation: board.json's "build.ldscript"
# key (which we set to the bare filename "nrf54l15_linker_script.ld") is
# a key PlatformIO's own generic build setup already treats as reserved,
# auto-generating its OWN "-T nrf54l15_linker_script.ld" (bare filename,
# no path -- it doesn't know this framework's nonstandard directory
# layout). ld processes -T flags in order and fails on the first bad one,
# which would explain why it never got to try our correctly-pathed one.
# Rather than fight that mechanism, let it use the bare filename and just
# make sure ld can FIND it -- ld's -T resolution also searches -L
# directories, not just cwd, which is what CORE_DIR in LIBPATH above does.
# UNCONFIRMED as the definitive mechanism (I can't inspect PlatformIO's
# internals directly), but consistent with all the evidence so far --
# worth testing before adding more complexity on top.

# compiler.libraries.ldflags -- Nordic's prebuilt SoftDevice Controller /
# MPSL static libs. Added as plain LIBS entries with full file paths (a
# well-established SCons mechanism -- a full path in LIBS gets passed
# through directly rather than treated as a -lname shorthand), rather than
# hand-crafting a _LIBFLAGS override. The earlier _LIBFLAGS approach caused
# an "Invalid argument" linker error from a quoting bug (my own added
# double-quotes around $_LIBDIRFLAGS glued two separate -L flags into one
# argument), and separately risked odd self-referential substitution by
# embedding $_LIBFLAGS inside a value being appended to $_LIBFLAGS itself.
#
# KNOWN SIMPLIFICATION: this drops the -Wl,--start-group/--end-group
# wrapping the real platform.txt recipe uses around the core archive + SDC
# libs + libm. That wrapping exists to resolve possible circular symbol
# references. Ordering below (core lib via Prepend below, "m" via the
# platform script's env.Replace, these SDC libs via Append here) puts SDC
# libs after both, which resolves the common direction (core code calling
# into SDC) but not the reverse. If linking fails with undefined-reference
# errors (a different failure mode than the quoting error above), that's
# the signal --start-group/--end-group needs to come back -- via LINKFLAGS
# positioned correctly relative to $SOURCES, not via _LIBFLAGS again.
env.Append(LIBS=[
    env.File(f"{NORDIC_SDC_DIR}/libsoftdevice_controller_multirole.a"),
    env.File(f"{NORDIC_SDC_DIR}/libmpsl_fem_common.a"),
    env.File(f"{NORDIC_SDC_DIR}/libmpsl.a"),
])

# Framework source: compile BOTH the core's own .c/.cpp files AND the
# variant's .c/.cpp files into libraries and link them in. Previously only
# CORE_DIR was built -- VARIANT_DIR was on CPPPATH (so its headers were
# visible) but its actual source files were never compiled or linked,
# which is exactly why generic core code calling board-specific functions
# (xiaoNrf54l15SaveBoardState etc. -- those live in the variant, per
# standard Arduino core/variant convention) failed with undefined
# references at link time.
env.Prepend(LIBS=[
    env.BuildLibrary(
        join("$BUILD_DIR", "FrameworkArduinoVariant"),
        VARIANT_DIR
    ),
    env.BuildLibrary(
        join("$BUILD_DIR", "FrameworkArduino"),
        CORE_DIR
    ),
])