Import("env")
import subprocess

try:
    git_hash = subprocess.check_output(
        ["git", "rev-parse", "HEAD"],
        stderr=subprocess.DEVNULL,
        cwd=env.subst("$PROJECT_DIR"),
    ).decode().strip()[:8]
except Exception:
    git_hash = "00000000"

env.Append(CPPDEFINES=[("GIT_HASH", '\\"' + git_hash + '\\"')])
print(f"[extra_scripts] GIT_HASH = {git_hash}")

def patch_flash_mode_qio(source, target, env):
    import sys, os, subprocess
    # sys.executable = ~/.platformio/penv/Scripts/python.exe → 2つ上が ~/.platformio
    pio_home = os.path.dirname(os.path.dirname(os.path.dirname(sys.executable)))
    esptool_script = os.path.join(pio_home, "packages", "tool-esptoolpy", "esptool.py")
    firmware = target[0].get_abspath()
    old = firmware.replace(".bin", ".old.bin")
    tmp = firmware + ".tmp"
    try:
        import shutil
        shutil.copy2(firmware, old)
        subprocess.check_call([
            sys.executable, esptool_script,
            "--chip", "esp32s3",
            "merge_bin",
            "--flash_mode", "qio",
            "-o", tmp,
            "0x0", firmware,
        ])
        os.replace(tmp, firmware)
        print("[extra_scripts] spi_mode → QIO (esptool)")
    except Exception as e:
        print(f"[extra_scripts] esptool patch failed: {e}")
        if os.path.exists(tmp):
            os.remove(tmp)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", patch_flash_mode_qio)
