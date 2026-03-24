from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Callable, Optional

try:
    import winreg
except ImportError:
    winreg = None

from hub_utils import get_bundle_dir, get_hub_data_dir, is_frozen


_NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0
_RUNTIME_ROOT = Path.home() / ".infengine" / "runtime"


def _first_existing_path(paths: list[str]) -> Optional[str]:
    for path in paths:
        if path and os.path.isfile(path):
            return path
    return None


def _existing_dirs(paths: list[str]) -> list[str]:
    return [path for path in paths if path and os.path.isdir(path)]


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
    return (
        "python-3.12.8-amd64.exe",
        "https://www.python.org/ftp/python/3.12.8/python-3.12.8-amd64.exe",
    )


def _is_embeddable_python_root(python_root: str) -> bool:
    try:
        return any(name.lower().endswith("._pth") for name in os.listdir(python_root))
    except OSError:
        return False


class PythonRuntimeError(RuntimeError):
    pass


class PythonRuntimeManager:
    def __init__(self) -> None:
        _RUNTIME_ROOT.mkdir(parents=True, exist_ok=True)

    def bundled_runtime_dir(self) -> str:
        return os.path.join(get_bundle_dir(), "InfEngineHubData", "runtime")

    def installed_runtime_dir(self) -> str:
        return os.path.join(get_hub_data_dir(), "runtime")

    def installer_path(self) -> str:
        name, _ = _runtime_installer_info_for_machine()
        if is_frozen():
            bundled = self.bundled_installer_path()
            if os.path.isfile(bundled):
                return bundled
        return str(_RUNTIME_ROOT / name)

    def bundled_installer_path(self) -> str:
        name, _ = _runtime_installer_info_for_machine()
        return os.path.join(self.bundled_runtime_dir(), name)

    def _get_pip_candidates(self) -> list[str]:
        return [
            os.path.join(self.installed_runtime_dir(), "get-pip.py"),
            os.path.join(self.bundled_runtime_dir(), "get-pip.py"),
            str(_RUNTIME_ROOT / "get-pip.py"),
        ]

    def _wheelhouse_dirs(self) -> list[str]:
        return _existing_dirs([
            os.path.join(self.installed_runtime_dir(), "wheels"),
            os.path.join(self.bundled_runtime_dir(), "wheels"),
            str(_RUNTIME_ROOT / "wheels"),
        ])

    def private_runtime_root(self) -> str:
        return os.path.join(get_hub_data_dir(), "python312")

    def private_runtime_python(self) -> str:
        if sys.platform == "win32":
            return os.path.join(self.private_runtime_root(), "python.exe")
        return os.path.join(self.private_runtime_root(), "bin", "python")

    def _private_runtime_candidates(self) -> list[str]:
        root = self.private_runtime_root()
        candidates = [self.private_runtime_python()]

        if sys.platform == "win32":
            candidates.extend(
                [
                    os.path.join(root, "Python.exe"),
                    os.path.join(root, "Python312", "python.exe"),
                ]
            )
        else:
            candidates.append(os.path.join(root, "bin", "python"))

        if os.path.isdir(root):
            for current_root, _dirs, files in os.walk(root):
                for filename in files:
                    if sys.platform == "win32":
                        if filename.lower() != "python.exe":
                            continue
                    elif filename != "python":
                        continue
                    candidates.append(os.path.join(current_root, filename))

        return self._dedupe_candidates(candidates)

    def venv_template_root(self) -> str:
        return os.path.join(get_hub_data_dir(), "runtime", "venv_template")

    def venv_template_python(self) -> str:
        if sys.platform == "win32":
            return os.path.join(self.venv_template_root(), "Scripts", "python.exe")
        return os.path.join(self.venv_template_root(), "bin", "python")

    def has_runtime(self) -> bool:
        return bool(self.get_runtime_path())

    def has_venv_template(self) -> bool:
        return self._is_valid_venv(self.venv_template_root())

    def get_runtime_path(self) -> Optional[str]:
        for candidate in self._candidate_paths():
            if self._is_valid_python312(candidate):
                return candidate
        return None

    def ensure_runtime(self) -> str:
        python_exe = self.get_runtime_path()
        if python_exe and self.has_venv_template():
            return python_exe

        if not python_exe:
            installer = self.prepare_installer()
            self.install_runtime(installer)

            python_exe = self.get_runtime_path()
            if not python_exe:
                raise PythonRuntimeError(
                    "Python 3.12 installation completed, but python.exe was not detected.\n"
                    "Please verify that Python 3.12 was installed successfully."
                )

        self._ensure_venv_template(python_exe)
        return python_exe

    def prepare_installer(
        self,
        *,
        on_progress: Optional[Callable[[int, int], None]] = None,
    ) -> str:
        if is_frozen():
            installer = self.bundled_installer_path()
            if os.path.isfile(installer):
                return installer

        return self.download_installer(on_progress=on_progress)

    def download_installer(
        self,
        *,
        on_progress: Optional[Callable[[int, int], None]] = None,
    ) -> str:
        installer = Path(self.installer_path())
        _, installer_url = _runtime_installer_info_for_machine()
        if installer.is_file():
            return str(installer)

        tmp_path = installer.with_suffix(installer.suffix + ".tmp")
        req = urllib.request.Request(installer_url)
        req.add_header("User-Agent", "InfEngine-Hub/1.0")

        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                total = int(resp.headers.get("Content-Length", "0") or 0)
                downloaded = 0
                chunk_size = 1024 * 1024
                with open(tmp_path, "wb") as f:
                    while True:
                        chunk = resp.read(chunk_size)
                        if not chunk:
                            break
                        f.write(chunk)
                        downloaded += len(chunk)
                        if on_progress is not None and total > 0:
                            on_progress(downloaded, total)
        except urllib.error.URLError as exc:
            if "unknown url type: https" in str(exc).lower():
                raise PythonRuntimeError(
                    "Failed to download Python 3.12 installer because HTTPS support is unavailable in the packaged Hub. "
                    "The SSL runtime was not bundled correctly."
                ) from exc
            raise PythonRuntimeError(
                f"Failed to download Python 3.12 installer.\n{exc}"
            ) from exc
        except OSError as exc:
            raise PythonRuntimeError(
                f"Failed to download Python 3.12 installer.\n{exc}"
            ) from exc

        os.replace(tmp_path, installer)
        return str(installer)

    def install_runtime(self, installer_path: str) -> None:
        if sys.platform != "win32":
            raise PythonRuntimeError("Automatic Python installation is only supported on Windows.")

        target_dir = self.private_runtime_root()
        python_exe = os.path.join(target_dir, "python.exe")

        shutil.rmtree(target_dir, ignore_errors=True)
        os.makedirs(target_dir, exist_ok=True)

        args = [
            installer_path,
            "/quiet",
            "InstallAllUsers=0",
            f'TargetDir={target_dir}',
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
            "SimpleInstall=1",
        ]
        self._run_command(args, timeout=1800)

        if not os.path.isfile(python_exe):
            raise PythonRuntimeError(
                "Python 3.12 executable was not found after installation:\n"
                f"{python_exe}"
            )

    def create_venv(self, venv_path: str) -> str:
        python_exe = self.ensure_runtime()
        template_root = self.venv_template_root()

        if not self._is_valid_venv(template_root):
            self._ensure_venv_template(python_exe)

        try:
            shutil.copytree(template_root, venv_path)
        except OSError as exc:
            raise PythonRuntimeError(
                f"Failed to copy the prepared virtual environment template to {venv_path}.\n{exc}"
            ) from exc

        self._rewrite_pyvenv_cfg(venv_path, python_exe)

        if sys.platform == "win32":
            venv_python = os.path.join(venv_path, "Scripts", "python.exe")
        else:
            venv_python = os.path.join(venv_path, "bin", "python")

        if not os.path.isfile(venv_python):
            raise PythonRuntimeError(
                f"Virtual environment creation finished, but python.exe was not found at {venv_python}."
            )
        return venv_python

    def _ensure_venv_template(self, python_exe: str) -> None:
        template_root = self.venv_template_root()
        if self._is_valid_venv(template_root):
            return

        os.makedirs(os.path.dirname(template_root), exist_ok=True)
        temp_root = template_root + ".tmp"
        shutil.rmtree(temp_root, ignore_errors=True)
        shutil.rmtree(template_root, ignore_errors=True)

        completed = self._run_command(
            [python_exe, "-m", "venv", "--copies", temp_root],
            timeout=600,
        )
        if completed.returncode != 0:
            details = self._summarize_output(completed.stderr or completed.stdout)
            raise PythonRuntimeError(
                "Failed to prepare the shared virtual environment template.\n"
                f"Exit code: {completed.returncode}\n"
                f"{details}"
            )

        self._rewrite_pyvenv_cfg(temp_root, python_exe)
        os.replace(temp_root, template_root)

    def _rewrite_pyvenv_cfg(self, venv_root: str, base_python: str) -> None:
        cfg_path = os.path.join(venv_root, "pyvenv.cfg")
        base_home = os.path.dirname(base_python)
        version = self._get_python_version(base_python)
        command = f'"{base_python}" -m venv --copies "{venv_root}"'

        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write(f"home = {base_home}\n")
            f.write("include-system-site-packages = false\n")
            f.write(f"version = {version}\n")
            f.write(f"executable = {base_python}\n")
            f.write(f"command = {command}\n")

    def _get_python_version(self, python_exe: str) -> str:
        completed = self._run_command(
            [python_exe, "-c", "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')"],
            timeout=20,
            raise_on_error=False,
        )
        if completed.returncode != 0:
            raise PythonRuntimeError(
                f"Failed to query the Python version for {python_exe}.\n"
                f"{self._summarize_output(completed.stderr or completed.stdout)}"
            )
        return (completed.stdout or "").strip()

    def _is_valid_venv(self, venv_root: str) -> bool:
        cfg_path = os.path.join(venv_root, "pyvenv.cfg")
        python_exe = self.venv_template_python() if venv_root == self.venv_template_root() else (
            os.path.join(venv_root, "Scripts", "python.exe") if sys.platform == "win32" else os.path.join(venv_root, "bin", "python")
        )
        return os.path.isfile(cfg_path) and os.path.isfile(python_exe)

    def _candidate_paths(self) -> list[str]:
        candidates: list[str] = []

        candidates.extend(self._private_runtime_candidates())

        env_candidate = os.environ.get("INFENGINE_PYTHON312")
        if env_candidate:
            candidates.append(env_candidate)

        if is_frozen():
            return self._dedupe_candidates(candidates)

        candidates.extend(self._registry_candidates())

        for root in filter(None, [os.environ.get("ProgramFiles"), os.environ.get("LocalAppData")]):
            if root == os.environ.get("LocalAppData"):
                candidates.append(os.path.join(root, "Programs", "Python", "Python312", "python.exe"))
            else:
                candidates.append(os.path.join(root, "Python312", "python.exe"))

        py_launcher = self._python_from_launcher()
        if py_launcher:
            candidates.append(py_launcher)

        return self._dedupe_candidates(candidates)

    @staticmethod
    def _dedupe_candidates(candidates: list[str]) -> list[str]:
        deduped: list[str] = []
        seen: set[str] = set()
        for candidate in candidates:
            normalized = os.path.normcase(os.path.abspath(candidate))
            if normalized in seen:
                continue
            seen.add(normalized)
            deduped.append(candidate)
        return deduped

    def _registry_candidates(self) -> list[str]:
        if winreg is None:
            return []

        candidates: list[str] = []
        keys = [
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Python\PythonCore\3.12\InstallPath"),
            (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Python\PythonCore\3.12\InstallPath"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Python\PythonCore\3.12\InstallPath"),
        ]

        for hive, subkey in keys:
            try:
                with winreg.OpenKey(hive, subkey) as key:
                    install_path, _ = winreg.QueryValueEx(key, None)
            except OSError:
                continue

            if install_path:
                candidates.append(os.path.join(install_path, "python.exe"))
        return candidates

    def _python_from_launcher(self) -> Optional[str]:
        completed = self._run_command(
            ["py", "-3.12", "-c", "import sys; print(sys.executable)"],
            timeout=20,
            raise_on_error=False,
        )
        if completed.returncode != 0:
            return None

        value = (completed.stdout or "").strip().splitlines()
        if not value:
            return None
        return value[-1].strip()

    def _is_valid_python312(self, python_exe: str) -> bool:
        if not python_exe or not os.path.isfile(python_exe):
            return False

        if _is_embeddable_python_root(os.path.dirname(python_exe)):
            return False

        completed = self._run_command(
            [python_exe, "-c", "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"],
            timeout=20,
            raise_on_error=False,
        )
        if completed.returncode != 0:
            return False
        return (completed.stdout or "").strip() == "3.12"

    def _run_command(
        self,
        args: list[str],
        *,
        timeout: int,
        raise_on_error: bool = True,
    ) -> subprocess.CompletedProcess:
        kwargs: dict = {
            "stdin": subprocess.DEVNULL,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.PIPE,
            "text": True,
        }
        if sys.platform == "win32":
            kwargs["creationflags"] = _NO_WINDOW

        try:
            return subprocess.run(args, timeout=timeout, check=raise_on_error, **kwargs)
        except FileNotFoundError as exc:
            if not raise_on_error:
                return subprocess.CompletedProcess(args=args, returncode=1, stdout="", stderr=str(exc))
            raise PythonRuntimeError(
                f"Command not found.\n{' '.join(args)}\n{exc}"
            ) from exc
        except OSError as exc:
            if not raise_on_error:
                return subprocess.CompletedProcess(args=args, returncode=1, stdout="", stderr=str(exc))
            raise PythonRuntimeError(
                f"Failed to execute command.\n{' '.join(args)}\n{exc}"
            ) from exc
        except subprocess.TimeoutExpired as exc:
            raise PythonRuntimeError(
                f"Command timed out after {timeout} seconds.\n{' '.join(args)}"
            ) from exc
        except subprocess.CalledProcessError as exc:
            details = self._summarize_output(exc.stderr or exc.stdout)
            raise PythonRuntimeError(
                f"Command failed with exit code {exc.returncode}.\n{' '.join(args)}\n{details}"
            ) from exc

    @staticmethod
    def _summarize_output(output: str) -> str:
        text = (output or "").strip()
        if not text:
            return "No diagnostic output was produced."
        lines = text.splitlines()
        return "\n".join(lines[-20:])