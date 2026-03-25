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
from pathlib import Path


_TEMPLATE_BUILDER_PACKAGES = [
    "nuitka",
    "ordered-set",
    "Pillow",
    "imageio",
    "av",
]


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


def _pth_files(python_root: str) -> list[str]:
    if not python_root or not os.path.isdir(python_root):
        return []
    return [
        os.path.join(python_root, name)
        for name in os.listdir(python_root)
        if name.lower().endswith("._pth") and os.path.isfile(os.path.join(python_root, name))
    ]


def _enable_site_for_embedded_runtime(python_root: str) -> None:
    if not _is_embeddable_python_root(python_root):
        return

    required_lines = ["python312.zip", ".", "Lib", "Lib/site-packages"]
    for pth_path in _pth_files(python_root):
        with open(pth_path, "r", encoding="utf-8") as f:
            raw_lines = [line.rstrip("\r\n") for line in f]

        output: list[str] = []
        seen: set[str] = set()
        for line in raw_lines:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                output.append(line)
                continue
            if stripped == "import site":
                continue
            if stripped not in seen:
                output.append(stripped)
                seen.add(stripped)

        for item in required_lines:
            if item not in seen:
                output.append(item)
                seen.add(item)
        output.append("import site")

        with open(pth_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(output).rstrip() + "\n")


def _is_python312(python_exe: str) -> bool:
    if not os.path.isfile(python_exe):
        return False

    _enable_site_for_embedded_runtime(os.path.dirname(python_exe))

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


def _installed_python_candidates() -> list[str]:
    candidates: list[str] = []

    local_app_data = os.environ.get("LOCALAPPDATA")
    program_files = os.environ.get("ProgramFiles")
    if local_app_data:
        candidates.append(os.path.join(local_app_data, "Programs", "Python", "Python312", "python.exe"))
    if program_files:
        candidates.append(os.path.join(program_files, "Python312", "python.exe"))

    if sys.platform == "win32":
        try:
            completed = subprocess.run(
                ["py", "-3.12", "-c", "import sys; print(sys.executable)"],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=20,
                creationflags=0x08000000,
            )
            if completed.returncode == 0:
                value = (completed.stdout or "").strip().splitlines()
                if value:
                    candidates.append(value[-1].strip())
        except (OSError, subprocess.SubprocessError):
            pass

        try:
            import winreg

            keys = [
                (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Python\PythonCore\3.12\InstallPath"),
                (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Python\PythonCore\3.12\InstallPath"),
                (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Python\PythonCore\3.12\InstallPath"),
            ]
            for hive, subkey in keys:
                try:
                    with winreg.OpenKey(hive, subkey) as key:
                        install_path, _ = winreg.QueryValueEx(key, None)
                    if install_path:
                        candidates.append(os.path.join(install_path, "python.exe"))
                except OSError:
                    continue
        except ImportError:
            pass

    deduped: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        normalized = os.path.normcase(os.path.abspath(candidate))
        if normalized in seen:
            continue
        seen.add(normalized)
        deduped.append(candidate)
    return deduped


def _find_copy_source_python(excluded_roots: list[str]) -> str | None:
    excluded = {os.path.normcase(os.path.abspath(root)) for root in excluded_roots}
    for candidate in _installed_python_candidates():
        if not _is_python312(candidate):
            continue
        candidate_root = os.path.normcase(os.path.abspath(os.path.dirname(candidate)))
        if candidate_root in excluded:
            continue
        return candidate
    return None


def _copy_python_installation(source_python: str, target_root: str) -> str:
    source_root = os.path.dirname(source_python)
    shutil.rmtree(target_root, ignore_errors=True)
    shutil.copytree(
        source_root,
        target_root,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"),
    )
    copied_python = os.path.join(target_root, os.path.basename(source_python))
    _enable_site_for_embedded_runtime(target_root)
    if _is_python312(copied_python):
        return copied_python
    found = _find_installed_python(target_root)
    if found:
        return found
    raise RuntimeError(
        "Copied an existing Python 3.12 installation, but python.exe was not detected afterwards.\n"
        f"Source: {source_root}\nTarget: {target_root}"
    )


def _bundled_runtime_python(runtime_dir: str) -> str | None:
    bundled_root = os.path.join(runtime_dir, "python312")
    return _find_installed_python(bundled_root)


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


def _user_runtime_dir() -> str:
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        return os.path.join(local_app_data, "InfEngineHub", "runtime")
    return str(Path.home() / ".infengine" / "runtime")


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


def _has_module(python_exe: str, module_name: str) -> bool:
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
            [python_exe, "-c", f"import importlib.util; print(int(importlib.util.find_spec('{module_name}') is not None))"],
            timeout=20,
            **kwargs,
        )
    except (OSError, subprocess.SubprocessError):
        return False

    return completed.returncode == 0 and (completed.stdout or "").strip() == "1"


def _create_runtime_env(python_exe: str, target_root: str) -> None:
    _enable_site_for_embedded_runtime(os.path.dirname(python_exe))

    commands: list[list[str]] = []
    if not _is_embeddable_python_root(os.path.dirname(python_exe)):
        commands.append([python_exe, "-m", "venv", "--copies", target_root])
    if _has_module(python_exe, "virtualenv"):
        commands.append([python_exe, "-m", "virtualenv", "--always-copy", target_root])

    last_error = ""
    for args in commands:
        kwargs = {
            "stdin": subprocess.DEVNULL,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.PIPE,
            "text": True,
        }
        if sys.platform == "win32":
            kwargs["creationflags"] = 0x08000000
        completed = subprocess.run(args, timeout=600, **kwargs)
        if completed.returncode == 0:
            return
        last_error = (completed.stderr or completed.stdout or "").strip()

    if not commands:
        raise RuntimeError(
            "The bundled Python runtime does not provide stdlib venv or virtualenv.\n"
            "For an embeddable runtime, include virtualenv in Lib/site-packages before packaging."
        )

    raise RuntimeError(
        "Failed to prepare the reusable venv template.\n"
        f"{last_error}"
    )


def _wheelhouse_dirs(runtime_dir: str, bundled_runtime_dir: str) -> list[str]:
    candidates = [
        os.path.join(runtime_dir, "wheels"),
        os.path.join(bundled_runtime_dir, "wheels"),
    ]
    return [path for path in candidates if os.path.isdir(path)]


def _install_template_packages(venv_root: str, wheelhouse_dirs: list[str]) -> None:
    if sys.platform == "win32":
        venv_python = os.path.join(venv_root, "Scripts", "python.exe")
    else:
        venv_python = os.path.join(venv_root, "bin", "python")

    args = [
        venv_python,
        "-m",
        "pip",
        "install",
        "--disable-pip-version-check",
        "--no-input",
        "--prefer-binary",
    ]
    if wheelhouse_dirs:
        args.append("--no-index")
        for wheelhouse in wheelhouse_dirs:
            args.extend(["--find-links", wheelhouse])

    args.extend(_TEMPLATE_BUILDER_PACKAGES)

    kwargs = {
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "text": True,
    }
    if sys.platform == "win32":
        kwargs["creationflags"] = 0x08000000

    completed = subprocess.run(args, timeout=1800, **kwargs)
    if completed.returncode != 0:
        raise RuntimeError(
            "Failed to install build support packages into the reusable venv template.\n"
            f"{(completed.stderr or completed.stdout or '').strip()}"
        )


def install_runtime_for_app(app_dir: str, progress_callback=None) -> None:
    installer_name, installer_url = _runtime_installer_info_for_machine()
    hub_data_dir = os.path.join(app_dir, "InfEngineHubData")
    bundled_runtime_dir = os.path.join(hub_data_dir, "runtime")
    runtime_dir = _user_runtime_dir()
    python_root = os.path.join(runtime_dir, "python312")
    template_dir = os.path.join(runtime_dir, "venv_template")
    installer_path = os.path.join(runtime_dir, installer_name)
    bundled_installer_path = os.path.join(bundled_runtime_dir, installer_name)
    python_exe = os.path.join(python_root, "python.exe")

    os.makedirs(runtime_dir, exist_ok=True)

    bundled_python_exe = _bundled_runtime_python(bundled_runtime_dir)

    if not _is_python312(python_exe) and bundled_python_exe:
        _emit(progress_callback, "Copying bundled Python 3.12 runtime...")
        python_exe = _copy_python_installation(bundled_python_exe, python_root)

    # 1. Download the full Python installer (skipped if already cached).
    if not os.path.isfile(installer_path) and os.path.isfile(bundled_installer_path):
        shutil.copy2(bundled_installer_path, installer_path)
    if not os.path.isfile(installer_path):
        _emit(progress_callback, f"Downloading Python 3.12 for {platform.machine()}...")
        _download_file(installer_url, installer_path)

    # 2. Install into the app-private python312/ directory.
    if not _is_python312(python_exe):
        copy_source = _find_copy_source_python([python_root])
        if copy_source:
            _emit(progress_callback, f"Copying existing Python 3.12 from {os.path.dirname(copy_source)}...")
            python_exe = _copy_python_installation(copy_source, python_root)

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
    _create_runtime_env(python_exe, template_dir)
    _install_template_packages(template_dir, _wheelhouse_dirs(runtime_dir, bundled_runtime_dir))
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