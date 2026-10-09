"""Settings for the Python scripts, as Env.ps1 does for the PowerShell ones.

Settings come from environment variables; a .env file at the repository root
(KEY=value lines, see .env.example) is read first, and a variable already set in
the environment takes precedence over it.
"""
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _load_env_file():
    path = os.path.join(REPO, '.env')
    if not os.path.isfile(path):
        return
    with open(path, encoding='utf-8') as f:
        for line in f:
            text = line.strip()
            if not text or text.startswith('#') or '=' not in text:
                continue
            name, value = text.split('=', 1)
            name, value = name.strip(), value.strip().strip('"').strip("'")
            if name and not os.environ.get(name):
                os.environ[name] = value


_load_env_file()


def setting(name, default=None):
    return os.environ.get(name) or default


def game_root():
    """The shadPS4 game folder: the folder holding the game's eboot.bin."""
    value = setting('NORTHSTAR_PS4_GAME_ROOT')
    if not value:
        raise SystemExit('NORTHSTAR_PS4_GAME_ROOT is not set (the shadPS4 game folder); see .env.example')
    return value
