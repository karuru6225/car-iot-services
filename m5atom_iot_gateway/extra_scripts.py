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
