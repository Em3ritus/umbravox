"""PlatformIO post-build guard: ELF usage is not the size of firmware.bin."""
Import('env')
import sys
from pathlib import Path
sys.path.insert(0, str(Path(env.subst('$PROJECT_DIR')) / 'tools'))
from check_app_size import check


def verify(source, target, env):
    build = Path(env.subst('$BUILD_DIR'))
    used, limit = check(build / 'firmware.bin', build / 'partitions.bin')
    print(f'Actual firmware: {used:,}/{limit:,} bytes; {limit-used:,} bytes free')


env.Depends('$BUILD_DIR/${PROGNAME}.bin', '$BUILD_DIR/partitions.bin')
env.AddPostAction('$BUILD_DIR/${PROGNAME}.bin', verify)
