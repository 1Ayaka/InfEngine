"""Header and bottom action bar for the launcher."""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLabel, QSizePolicy, QApplication, QCheckBox, QGraphicsOpacityEffect
)
from PySide6.QtCore import Qt, QPropertyAnimation, Property, QEasingCurve
from PySide6.QtGui import QPainter, QColor, QBrush, QPaintEvent, QPixmap
from style import StyleManager


class ToggleSwitch(QCheckBox):
    """A custom animated toggle switch."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(44, 24)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self._position = 23.0  # Default checked position
        self.animation = QPropertyAnimation(self, b"position")
        self.animation.setEasingCurve(QEasingCurve.Type.InOutQuad)
        self.animation.setDuration(200)
        self.stateChanged.connect(self._on_state_change)

    @Property(float)
    def position(self):
        return self._position

    @position.setter
    def position(self, pos):
        self._position = pos
        self.update()

    def hitButton(self, pos):
        return self.rect().contains(pos)

    def _on_state_change(self, value):
        self.animation.stop()
        if value:
            self.animation.setEndValue(23.0)
        else:
            self.animation.setEndValue(3.0)
        self.animation.start()

    def paintEvent(self, e: QPaintEvent):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)

        app = QApplication.instance()
        is_dark = getattr(app, 'is_dark_theme', True)

        if self.isChecked():
            bg_color = QColor("#ffffff") if is_dark else QColor("#37352f")
            thumb_color = QColor("#191919") if is_dark else QColor("#ffffff")
        else:
            bg_color = QColor("#555555") if is_dark else QColor("#e9e9e7")
            thumb_color = QColor("#cfcfcf") if is_dark else QColor("#ffffff")

        p.setBrush(QBrush(bg_color))
        p.drawRoundedRect(0, 0, self.width(), self.height(), 12, 12)

        p.setBrush(QBrush(thumb_color))
        p.drawEllipse(int(self._position), 3, 18, 18)
        p.end()


class ControlPane(QWidget):
    """Top header with title + bottom action buttons."""

    def __init__(self, viewmodel, style=None, parent=None):
        super().__init__(parent)
        self.viewmodel = viewmodel

        main_layout = QVBoxLayout(self)
        main_layout.setSpacing(0)
        main_layout.setContentsMargins(0, 0, 0, 0)

        # ── Header row ──
        header = QHBoxLayout()
        header.setContentsMargins(4, 0, 0, 12)

        title = QLabel("InfEngine")
        title.setObjectName("mainTitle")
        header.addWidget(title, alignment=Qt.AlignmentFlag.AlignLeft)

        subtitle = QLabel("Project Launcher")
        subtitle.setObjectName("subTitle")
        subtitle.setStyleSheet("padding-top: 8px;")
        header.addWidget(subtitle)
        header.addStretch()

        # ── Action buttons (right-aligned in header) ──
        theme_layout = QHBoxLayout()
        theme_layout.setSpacing(8)
        
        self.theme_label = QLabel("Dark Mode")
        self.theme_label.setObjectName("themeLabel")
        
        self.theme_toggle = ToggleSwitch()
        self.theme_toggle.setChecked(True)
        self.theme_toggle.stateChanged.connect(self.toggle_theme)
        
        theme_layout.addWidget(self.theme_label)
        theme_layout.addWidget(self.theme_toggle)
        header.addLayout(theme_layout)

        spacer_label0 = QLabel("")
        spacer_label0.setFixedWidth(16)
        header.addWidget(spacer_label0)

        self.btn_delete = QPushButton("Delete")
        self.btn_delete.setObjectName("dangerBtn")
        self.btn_delete.setFixedHeight(36)
        self.btn_delete.setMinimumWidth(90)
        self.btn_delete.clicked.connect(lambda: self.viewmodel.delete_project(self))
        header.addWidget(self.btn_delete)

        spacer_label = QLabel("")
        spacer_label.setFixedWidth(8)
        header.addWidget(spacer_label)

        self.btn_new = QPushButton("+ New Project")
        self.btn_new.setObjectName("primaryBtn")
        self.btn_new.setFixedHeight(36)
        self.btn_new.setMinimumWidth(130)
        self.btn_new.clicked.connect(lambda: self.viewmodel.create_project(self))
        header.addWidget(self.btn_new)

        spacer_label2 = QLabel("")
        spacer_label2.setFixedWidth(8)
        header.addWidget(spacer_label2)

        self.btn_launch = QPushButton("▶  Launch")
        self.btn_launch.setObjectName("normalBtn")
        self.btn_launch.setFixedHeight(36)
        self.btn_launch.setMinimumWidth(110)
        self.btn_launch.clicked.connect(lambda: self.viewmodel.launch_project(self))
        header.addWidget(self.btn_launch)

        main_layout.addLayout(header)

    def toggle_theme(self, state):
        app = QApplication.instance()
        # state is an integer (0 for unchecked, 2 for checked)
        is_dark = bool(state)
        if getattr(app, 'is_dark_theme', True) == is_dark:
            return

        window = self.window()
        
        # 1. Grab current screen
        pixmap = window.grab()
        
        # 2. Create overlay
        self._overlay = QLabel(window)
        self._overlay.setPixmap(pixmap)
        self._overlay.setGeometry(window.rect())
        self._overlay.show()
        
        # 3. Change theme
        app.is_dark_theme = is_dark
        app.setStyleSheet(StyleManager.get_stylesheet(is_dark))
        
        # Force process events so the new theme is rendered underneath
        app.processEvents()
        
        # 4. Animate opacity
        effect = QGraphicsOpacityEffect(self._overlay)
        self._overlay.setGraphicsEffect(effect)
        
        self._theme_anim = QPropertyAnimation(effect, b"opacity")
        self._theme_anim.setDuration(300)
        self._theme_anim.setStartValue(1.0)
        self._theme_anim.setEndValue(0.0)
        self._theme_anim.setEasingCurve(QEasingCurve.Type.InOutQuad)
        self._theme_anim.finished.connect(self._overlay.deleteLater)
        self._theme_anim.start()
