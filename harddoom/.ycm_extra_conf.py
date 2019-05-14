import os

CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))

include_dirs = [
    './arch/x86/include',
    './arch/x86/include/generated',
    './arch/x86/include/generated/uapi',
    './arch/x86/include/uapi',
    './include',
    './include/generated/uapi',
    './include/uapi',
]


include_files = [
    './include/linux/kconfig.h',
]

flags = [
    '-D__KERNEL__',
    '-std=gnu99',
    '-xc',
    '-nostdinc',
]

def FlagsForFile( filename, **kwargs ):
    """
    Given a source file, retrieves the flags necessary for compiling it.
    """
    kernel_dir = os.path.join(CURRENT_DIR, '../linux')
    for dir in include_dirs:
        flags.append('-I' + os.path.join(kernel_dir, dir))

    for file in include_files:
        flags.append('-include' + os.path.join(kernel_dir, file))

    return { 'flags': flags }
