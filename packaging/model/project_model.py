import datetime
from InfEngine.resources import resources_path, engine_font_path, engine_lib_path
import os
import sys
import json
import venv
import subprocess
import shutil, glob
from pathlib import Path


def _find_infengine_wheel() -> str:
    """Find the InfEngine wheel in the dist/ directory next to the engine source."""
    # The wheel lives in <engine_root>/dist/
    engine_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    dist_dir = os.path.join(engine_root, "dist")
    wheels = glob.glob(os.path.join(dist_dir, "infengine-*.whl"))
    if wheels:
        # Return the newest wheel
        wheels.sort(key=os.path.getmtime, reverse=True)
        return wheels[0]
    return ""


class ProjectModel:
    def __init__(self, db):
        self.db = db

    def add_project(self, name, path):
        return self.db.add_project(name, path)

    def delete_project(self, name):
        self.db.delete_project(name)

    
    def init_project_folder(self, project_name:str, project_path: str):
        import os
        import shutil

        project_dir = os.path.join(project_path, project_name)
        os.makedirs(project_dir, exist_ok=True)

        # Create subdirectories
        subdirs = ["ProjectSettings", "Logs", "Library", "Assets", "Basics"]
        for subdir in subdirs:
            os.makedirs(os.path.join(project_dir, subdir), exist_ok=True)

        # Copy .dll, .pyd from resources_path to Library
        dll_files = glob.glob(os.path.join(engine_lib_path, "*.dll"))
        pyd_files = glob.glob(os.path.join(engine_lib_path, "*.pyd"))

        for file in dll_files + pyd_files:
            shutil.copy(file, os.path.join(project_dir, "Library"))

        # Copy resources file from .resources to Basics(fonts, pictures, shaders)
        shutil.copytree(
            resources_path,
            os.path.join(project_dir, "Basics"),
            dirs_exist_ok=True,
            ignore=shutil.ignore_patterns("*.py", "*.pyc", "__pycache__")
        )

        # Create a README file in assets
        readme_path = os.path.join(project_dir, "Assets", "README.md")
        with open(readme_path, "w") as readme_file:
            readme_file.write("# Project Assets\n\n")
            readme_file.write("This folder contains all the assets for the project.\n")

        # Create .ini file in project path
        ini_path = os.path.join(project_dir, f"{project_name}.ini")
        with open(ini_path, "w", encoding="utf-8") as ini_file:
            """
            example content:
            [Project]
            name = MyProject
            path = /path/to/myproject
            created_at = 2023-01-01 12:00:00
            changed_at = 2023-01-01 12:00:00
            """
            ini_file.write("[Project]\n")
            ini_file.write(f"name = {project_name}\n")
            ini_file.write(f"path = {project_dir}\n")
            ini_file.write(f"created_at = {datetime.datetime.now()}\n")
            ini_file.write(f"changed_at = {datetime.datetime.now()}\n")

        # ── Create .venv and install InfEngine ──────────────────────────
        venv_path = os.path.join(project_dir, ".venv")
        venv.EnvBuilder(with_pip=True).create(venv_path)
        self._install_infengine_in_venv(project_dir)

        # ── Create VS Code workspace configuration ─────────────────────
        self._create_vscode_workspace(project_dir)

    # -----------------------------------------------------------------
    # Private helpers
    # -----------------------------------------------------------------

    @staticmethod
    def _get_venv_python(project_dir: str) -> str:
        """Return the Python executable inside the project's .venv."""
        venv_dir = os.path.join(project_dir, ".venv")
        if sys.platform == "win32":
            return os.path.join(venv_dir, "Scripts", "python.exe")
        return os.path.join(venv_dir, "bin", "python")

    @staticmethod
    def _install_infengine_in_venv(project_dir: str):
        """Install the InfEngine wheel into the project's .venv."""
        venv_python = ProjectModel._get_venv_python(project_dir)
        if not os.path.isfile(venv_python):
            print(f"[ProjectModel] venv python not found: {venv_python}")
            return

        wheel = _find_infengine_wheel()
        if wheel:
            # Install from the pre-built wheel (fast, offline-capable)
            subprocess.run(
                [venv_python, "-m", "pip", "install", "--force-reinstall", wheel],
                capture_output=True,
            )
            print(f"[ProjectModel] Installed InfEngine from wheel: {wheel}")
        else:
            # Fallback: install from the source tree (editable)
            engine_root = os.path.abspath(
                os.path.join(os.path.dirname(__file__), "..", "..")
            )
            subprocess.run(
                [venv_python, "-m", "pip", "install", "-e", engine_root],
                capture_output=True,
            )
            print(f"[ProjectModel] Installed InfEngine (editable) from: {engine_root}")

    @staticmethod
    def _create_vscode_workspace(project_dir: str):
        """
        Create .vscode/ config so that opening any file inside the project
        uses the correct Python interpreter and gets full InfEngine autocompletion.
        """
        vscode_dir = os.path.join(project_dir, ".vscode")
        os.makedirs(vscode_dir, exist_ok=True)

        # ── settings.json ───────────────────────────────────────────────
        venv_python = ProjectModel._get_venv_python(project_dir)
        settings = {
            "python.defaultInterpreterPath": venv_python,
            "python.analysis.typeCheckingMode": "basic",
            "python.analysis.autoImportCompletions": True,
            "python.analysis.diagnosticSeverityOverrides": {
                "reportMissingModuleSource": "none",
            },
            "editor.formatOnSave": True,
            "files.exclude": {
                "**/__pycache__": True,
                "**/*.pyc": True,
                "**/*.meta": True,
                ".venv": True,
                "Library": True,
                "Logs": True,
                "ProjectSettings": True,
                "Basics": True,
            },
        }
        settings_path = os.path.join(vscode_dir, "settings.json")
        with open(settings_path, "w", encoding="utf-8") as f:
            json.dump(settings, f, indent=4, ensure_ascii=False)

        # ── extensions.json ─────────────────────────────────────────────
        extensions = {
            "recommendations": [
                "ms-python.python",
                "ms-python.vscode-pylance",
            ]
        }
        extensions_path = os.path.join(vscode_dir, "extensions.json")
        with open(extensions_path, "w", encoding="utf-8") as f:
            json.dump(extensions, f, indent=4, ensure_ascii=False)

        # ── pyrightconfig.json (at project root) ────────────────────────
        pyright_config = {
            "venvPath": ".",
            "venv": ".venv",
            "pythonVersion": "3.12",
            "typeCheckingMode": "basic",
            "reportMissingModuleSource": False,
            "reportWildcardImportFromLibrary": False,
            "include": ["Assets"],
        }
        pyright_path = os.path.join(project_dir, "pyrightconfig.json")
        with open(pyright_path, "w", encoding="utf-8") as f:
            json.dump(pyright_config, f, indent=4, ensure_ascii=False)