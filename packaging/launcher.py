import sys
sys.dont_write_bytecode = True

import ctypes
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget,
    QVBoxLayout, QSizePolicy
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon, QFontDatabase, QFont

from ui_project_list import ProjectListPane
from database import ProjectDatabase
from style import StyleManager
from InfEngine.resources import icon_path, engine_font_path

from model.project_model import ProjectModel
from viewmodel.control_pane_viewmodel import ControlPaneViewModel
from view.control_pane_view import ControlPane


class GameEngineLauncher(QMainWindow):
    def __init__(self) -> None:
        self._own_app = False
        if QApplication.instance() is None:
            self._own_app = True
            self.app = QApplication(sys.argv)
        else:
            self.app = QApplication.instance()

        super().__init__()

        # Load custom engine font
        font_id = QFontDatabase.addApplicationFont(engine_font_path)
        if font_id >= 0:
            QFontDatabase.applicationFontFamilies(font_id)

        # # DPI awareness (Windows)
        # if sys.platform == "win32":
        #     ctypes.OleDLL("shcore").SetProcessDpiAwareness(1)

        # Apply global dark theme
        self.app.is_dark_theme = True
        self.app.setStyleSheet(StyleManager.get_stylesheet(self.app.is_dark_theme))

        self.setWindowTitle("InfEngine")
        self.setWindowIcon(QIcon(icon_path))
        self.resize(1080, 720)

        # Database connection
        self.db = ProjectDatabase()

        # Central widget
        central = QWidget(self)
        central.setObjectName("central")
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(28, 24, 28, 24)
        layout.setSpacing(16)

        # Project list (View)
        self.project_list = ProjectListPane(self.db, parent=central)

        # MVVM control pane
        model = ProjectModel(self.db)
        viewmodel = ControlPaneViewModel(model, self.project_list)
        self.controls = ControlPane(viewmodel, parent=central)

        # Size policies
        self.controls.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.project_list.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        # Layout: header on top, project list fills remaining space
        layout.addWidget(self.controls, 0)
        layout.addWidget(self.project_list, 1)

        # Cleanup on close
        self.app.aboutToQuit.connect(self._on_close)

    def run(self):
        self.show()
        if self._own_app:
            sys.exit(self.app.exec())

    def _on_close(self):
        self.db.close()


if __name__ == "__main__":
    launcher = GameEngineLauncher()
    launcher.run()
