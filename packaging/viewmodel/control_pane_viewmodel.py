import os
from PySide6.QtWidgets import (
    QMessageBox, QDialog, QVBoxLayout, QLabel, QProgressBar
)
from PySide6.QtCore import QThread, Signal, QObject, QTimer, Qt
from model.project_model import ProjectModel
from view.new_project_view import NewProjectView
import random


class CustomProgressDialog(QDialog):
    """Indeterminate progress dialog shown during project initialization."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Initializing")
        self.setWindowModality(Qt.WindowModal)
        self.setFixedSize(340, 110)

        self.label = QLabel("Preparing project...", self)
        self.label.setAlignment(Qt.AlignCenter)

        self.progress_bar = QProgressBar(self)
        self.progress_bar.setRange(0, 0)
        self.progress_bar.setTextVisible(False)

        layout = QVBoxLayout()
        layout.addWidget(self.label)
        layout.addWidget(self.progress_bar)
        self.setLayout(layout)

        self.messages = [
            "Setting up project structure...",
            "Copying engine libraries...",
            "Configuring virtual environment...",
            "Preparing asset folders...",
            "Almost there...",
        ]

        self.timer = QTimer(self)
        self.timer.timeout.connect(self._rotate_message)
        self.timer.start(2000)

    def _rotate_message(self):
        self.label.setText(random.choice(self.messages))


class InitProjectWorker(QObject):
    """Worker that runs project initialization on a background thread."""
    finished = Signal()

    def __init__(self, model, name, path):
        super().__init__()
        self.model = model
        self.name = name
        self.path = path

    def run(self):
        self.model.init_project_folder(self.name, self.path)
        self.finished.emit()


class ControlPaneViewModel:
    def __init__(self, model, project_list):
        self.model = model
        self.project_list = project_list

    def launch_project(self, parent):
        project_name = self.project_list.get_selected_project()
        if not project_name:
            QMessageBox.warning(parent, "No Selection", "Please select a project to launch.")
            return
        
        import subprocess
        import sys
        
        project_path = os.path.join(self.project_list.get_selected_project_path(), project_name)
        
        # Launch engine in a separate process to avoid interfering with launcher
        script = f'''
import sys
from InfEngine.engine import release_engine
from InfEngine.lib import LogLevel
release_engine(engine_log_level=LogLevel.Info, project_path=r"{project_path}")
'''
        # Start the engine as a separate process
        # Use same console to see debug output (no CREATE_NEW_CONSOLE)
        subprocess.Popen(
            [sys.executable, "-c", script]
        )

    def delete_project(self, parent):
        project_name = self.project_list.get_selected_project()
        if not project_name:
            QMessageBox.warning(parent, "No Selection", "Please select a project to delete.")
            return

        confirm = QMessageBox.question(
            parent,
            "Confirm Deletion",
            f"Are you sure you want to delete the project '{project_name}'?",
        )
        if confirm != QMessageBox.Yes:
            return

        self.model.delete_project(project_name)
        self.project_list.refresh()

    def create_project(self, parent):
        dialog = NewProjectView(parent)
        if dialog.exec() != QDialog.Accepted:
            return

        new_name, project_path = dialog.get_data()
        if not new_name:
            QMessageBox.warning(parent, "Missing Name", "Please enter a project name.")
            return
        if not project_path:
            QMessageBox.warning(parent, "Missing Location", "Please choose a project location.")
            return

        if not self.model.add_project(new_name, project_path):
            QMessageBox.critical(parent, "Duplicate Name", f"Project '{new_name}' already exists.")
            return

        progress_dialog = CustomProgressDialog(parent)
        progress_dialog.show()

        self.thread = QThread()
        self.worker = InitProjectWorker(self.model, new_name, project_path)
        self.worker.moveToThread(self.thread)

        self.thread.started.connect(self.worker.run)
        self.worker.finished.connect(self.thread.quit)
        self.worker.finished.connect(self.worker.deleteLater)
        self.thread.finished.connect(progress_dialog.accept)
        self.thread.finished.connect(self.thread.deleteLater)
        self.thread.finished.connect(self.project_list.refresh)

        self.thread.start()
