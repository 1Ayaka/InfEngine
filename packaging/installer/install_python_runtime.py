from __future__ import annotations

import argparse
import ctypes
import os
import platform
import shutil
import subprocess
import sys
import urllib.error
import urllib.request


def _first_existing_path(paths: list[str]) -> str | None:
    for path in paths:
        if path and os.path.isfile(path):
            return path
    return None


def _existing_dirs(paths: list[str]) -> list[str]:
    return [path for path in paths if path and os.path.isdir(path)]


def _is_embeddable_python_root(python_root: str) -> bool:
    try:
        return any(name.lower().endswith("._pth") for name in os.listdir(python_root))
    except OSError:
        return False


def _is_python312(python_exe: str) -> bool:
    if not os.path.isfile(python_exe):
        return False

    if _is_embeddable_python_root(os.path.dirname(python_exe)):
        return False

    kwargs = {
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "text": True,
    }
    if sys.platform == "win32":
        kwargs["creationflags"] = 0x08000000

    try:
        completed = subprocess.run(
            [python_exe, "-c", "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"],
            timeout=20,
            **kwargs,
        )
    except (OSError, subprocess.SubprocessError):
        return False

    return completed.returncode == 0 and (completed.stdout or "").strip() == "3.12"


def _find_installed_python(python_root: str) -> str | None:
    direct_candidates = [
        os.path.join(python_root, "python.exe"),
        os.path.join(python_root, "Python.exe"),
        os.path.join(python_root, "Python312", "python.exe"),
        os.path.join(python_root, "bin", "python"),
    ]
    for candidate in direct_candidates:
        if _is_python312(candidate):
            return candidate

    for root, _dirs, files in os.walk(python_root):
        for filename in files:
            if filename.lower() != "python.exe":
                continue
            candidate = os.path.join(root, filename)
            if _is_python312(candidate):
                return candidate

    return None


def _runtime_installer_info_for_machine() -> tuple[str, str]:
    """Return (filename, url) for the official Python 3.12 Windows installer."""
    machine = (platform.machine() or "").lower()
    if machine in {"amd64", "x86_64"}:
        return (
            "python-3.12.8-amd64.exe",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8-amd64.exe",
        )
    if machine in {"arm64", "aarch64"}:
        return (
            "python-3.12.8-arm64.exe",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8-arm64.exe",
        )
    if machine in {"x86", "i386", "i686"}:
        return (
            "python-3.12.8.exe",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8.exe",
        )
    raise RuntimeError(f"Unsupported Windows architecture: {machine}")


def _download_file(url: str, dest: str) -> None:
    """Download a file from *url* to *dest*."""
    req = urllib.request.Request(url)
    req.add_header("User-Agent", "InfEngine-Hub-Installer/1.0")
    try:
        with urllib.request.urlopen(req, timeout=120) as resp, open(dest, "wb") as f:
            shutil.copyfileobj(resp, f)
    except urllib.error.URLError as exc:
        if "unknown url type: https" in str(exc).lower():
            raise RuntimeError(
                "HTTPS download support is unavailable. "
                "The SSL runtime was not initialized correctly."
            ) from exc
        raise


# Python installer exit code indicating "success – reboot required".
_EXITCODE_REBOOT_REQUIRED = 3010


def _quote_burn(value: str) -> str:
    """Quote a value for the Burn bootstrapper command line."""
    if " " in value or '"' in value:
        return f'"{value}"'
    return value


def _build_installer_cmdline(
    installer_path: str,
    ui_flag: str,
    log_path: str,
    target_dir: str,
) -> str:
    """Build command line for the Python Burn bootstrapper.

    The Burn engine has its own command-line parser that differs from the
    standard C runtime rules used by ``subprocess.list2cmdline``.  In
    particular, ``TargetDir=`` needs the *value* quoted, not the whole
    ``property=value`` token.  We construct the command as a raw string
    so it is passed directly to ``CreateProcess``.
    """
    parts = [
        _quote_burn(installer_path),
        ui_flag,
        f"/log {_quote_burn(log_path)}",
        "InstallAllUsers=0",
        f"TargetDir={_quote_burn(target_dir)}",
        "AssociateFiles=0",
        "CompileAll=0",
        "Include_debug=0",
        "Include_dev=0",
        "Include_doc=0",
        "Include_launcher=0",
        "Include_pip=1",
        "Include_symbols=0",
        "Include_tcltk=1",
        "Include_test=0",
        "LauncherOnly=0",
        "PrependPath=0",
        "Shortcuts=0",
    ]
    return " ".join(parts)


def _run_hidden(cmd, *, timeout: int, ok_codes: tuple[int, ...] = (0,)) -> int:
    """Run *cmd* (a string or arg list) hidden and return the exit code."""
    kwargs = {
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "text": True,
    }
    if sys.platform == "win32":
        kwargs["creationflags"] = 0x08000000

    completed = subprocess.run(cmd, timeout=timeout, **kwargs)
    if completed.returncode not in ok_codes:
        details = (completed.stderr or completed.stdout or "").strip()
        cmdline = cmd if isinstance(cmd, str) else subprocess.list2cmdline(cmd)
        raise RuntimeError(
            f"Command failed with exit code {completed.returncode}: {cmdline}\n{details}"
        )
    return completed.returncode


def _show_message_box(title: str, message: str, icon: int = 0x40) -> None:
    if sys.platform != "win32":
        return
    try:
        ctypes.windll.user32.MessageBoxW(None, message, title, icon)
    except Exception:
        pass


def _emit(progress_callback, message: str) -> None:
    if progress_callback is not None:
        progress_callback(message)


def install_runtime_for_app(app_dir: str, progress_callback=None) -> None:
    installer_name, installer_url = _runtime_installer_info_for_machine()
    hub_data_dir = os.path.join(app_dir, "InfEngineHubData")
    runtime_dir = os.path.join(hub_data_dir, "runtime")
    python_root = os.path.join(hub_data_dir, "python312")
    template_dir = os.path.join(runtime_dir, "venv_template")
    installer_path = os.path.join(runtime_dir, installer_name)
    python_exe = os.path.join(python_root, "python.exe")

    os.makedirs(runtime_dir, exist_ok=True)

    # 1. Download the full Python installer (skipped if already cached).
    if not os.path.isfile(installer_path):
        _emit(progress_callback, f"Downloading Python 3.12 for {platform.machine()}...")
        _download_file(installer_url, installer_path)

    # 2. Install into the app-private python312/ directory.
    if not _is_python312(python_exe):
        _emit(progress_callback, "Installing Python 3.12...")
        shutil.rmtree(python_root, ignore_errors=True)
        os.makedirs(python_root, exist_ok=True)

        log_path = os.path.join(runtime_dir, "python_install.log")

        # Try quiet first, fall back to passive (shows progress bar) on failure.
        for ui_flag in ("/quiet", "/passive"):
            cmdline = _build_installer_cmdline(
                installer_path, ui_flag, log_path, python_root,
            )
            try:
                _run_hidden(
                    cmdline,
                    timeout=1800,
                    ok_codes=(0, _EXITCODE_REBOOT_REQUIRED),
                )
                break
            except RuntimeError:
                if ui_flag == "/passive":
                    raise
                _emit(progress_callback, "Silent install failed, retrying with progress UI...")

        # Locate python.exe – the installer may place it in a subdirectory.
        if not _is_python312(python_exe):
            found = _find_installed_python(python_root)
            if found:
                python_exe = found

    if not os.path.isfile(python_exe):
        log_hint = ""
        log_path = os.path.join(runtime_dir, "python_install.log")
        if os.path.isfile(log_path):
            log_hint = f"\nInstaller log: {log_path}"
        raise RuntimeError(
            "Python 3.12 executable was not found after installation:\n"
            f"{python_exe}{log_hint}"
        )

    # 3. Create the reusable venv template.
    _emit(progress_callback, "Preparing reusable venv template...")
    shutil.rmtree(template_dir, ignore_errors=True)
    _run_hidden([python_exe, "-m", "venv", "--copies", template_dir], timeout=600)
    _emit(progress_callback, "Private runtime is ready.")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-dir")
    args = parser.parse_args()

    if not args.app_dir:
        _show_message_box(
            "InfEngine Runtime Installer",
            "This program is an internal installer helper for InfEngine Hub.\n\n"
            "Please run InfEngineHubInstaller.exe instead of launching this file directly.",
            0x30,
        )
        return 1

    install_runtime_for_app(args.app_dir)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        _show_message_box(
            "InfEngine Runtime Installer Error",
            str(exc),
            0x10,
        )
        try:
            sys.stderr.write(str(exc) + "\n")
        except Exception:
            pass
        raise SystemExit(1)