"""Project list pane with search, modern Notion-themed cards, and folder-open."""

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QScrollArea,
    QSizePolicy, QFrame, QPushButton, QLineEdit
)
from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QDesktopServices

from database import ProjectDatabase


class _ProjectCard(QFrame):
    """A single project card with initials avatar, name, date, path."""

    def __init__(self, name: str, created_at: str, path: str, parent=None):
        super().__init__(parent)
        self.setObjectName("projectCard")
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.project_name = name
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFixedHeight(72)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(14)

        # --- Initials avatar ---
        initials = "".join(w[0] for w in name.split()[:2]).upper() or name[:2].upper()
        avatar = QPushButton(initials)
        avatar.setObjectName("cardAvatar")
        avatar.setFixedSize(44, 44)
        avatar.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, True)
        layout.addWidget(avatar)

        # --- Text block ---
        text_col = QVBoxLayout()
        text_col.setSpacing(2)
        text_col.setContentsMargins(0, 0, 0, 0)

        name_label = QLabel(name)
        name_label.setObjectName("cardName")
        text_col.addWidget(name_label)

        path_label = QLabel(path)
        path_label.setObjectName("cardPath")
        path_label.setToolTip(path)
        text_col.addWidget(path_label)

        layout.addLayout(text_col, 1)

        # --- Date ---
        date_str = created_at[:10] if created_at else ""
        date_label = QLabel(date_str)
        date_label.setObjectName("cardDate")
        layout.addWidget(date_label, alignment=Qt.AlignmentFlag.AlignRight)

        # --- Open-folder button ---
        open_btn = QPushButton("⌂")
        open_btn.setObjectName("cardOpenBtn")
        open_btn.setFixedSize(32, 32)
        open_btn.setToolTip("Open project folder")
        open_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        open_btn.clicked.connect(lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(path)))
        layout.addWidget(open_btn)

    # --- Selection state ---
    def set_selected(self, selected: bool):
        self.setProperty("selected", selected)
        self.style().unpolish(self)
        self.style().polish(self)


class ProjectListPane(QWidget):
    """Scrollable list of project cards with a search bar."""

    def __init__(self, db: ProjectDatabase, parent=None):
        super().__init__(parent)
        self.db = db
        self.selected_project = None
        self.project_cards: dict[str, _ProjectCard] = {}
        self._all_projects: list[tuple[str, str, str]] = []

        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
        self.setStyleSheet("background-color: transparent;")

        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(10)

        # --- Search bar ---
        self.search_edit = QLineEdit()
        self.search_edit.setObjectName("searchBox")
        self.search_edit.setPlaceholderText("  Search projects...")
        self.search_edit.setClearButtonEnabled(True)
        self.search_edit.setFixedHeight(36)
        self.search_edit.textChanged.connect(self._apply_filter)
        main_layout.addWidget(self.search_edit)

        # --- Scrollable card area ---
        scroll_area = QScrollArea()
        scroll_area.setWidgetResizable(True)
        main_layout.addWidget(scroll_area)

        self.container = QWidget()
        self.container.setStyleSheet("background: transparent;")
        self.card_layout = QVBoxLayout(self.container)
        self.card_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.card_layout.setSpacing(6)
        self.card_layout.setContentsMargins(0, 0, 4, 0)
        scroll_area.setWidget(self.container)

        self.refresh()

    # ------------------------------------------------------------------
    def refresh(self):
        self.project_cards.clear()
        while self.card_layout.count():
            item = self.card_layout.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()

        self._all_projects = self.db.all_projects()
        for name, created_at, path in self._all_projects:
            card = _ProjectCard(name, created_at, path)
            card.mousePressEvent = lambda _ev, n=name: self._on_select(n)
            card.mouseDoubleClickEvent = lambda _ev, n=name: self._on_double_click(n)
            self.card_layout.addWidget(card)
            self.project_cards[name] = card

        self.card_layout.addStretch()
        self._apply_filter(self.search_edit.text())

    # ------------------------------------------------------------------
    def _apply_filter(self, text: str):
        needle = text.strip().lower()
        for name, card in self.project_cards.items():
            card.setVisible(needle in name.lower() if needle else True)

    def _on_select(self, name: str):
        self.selected_project = name
        for n, card in self.project_cards.items():
            card.set_selected(n == name)

    def _on_double_click(self, name: str):
        """Select + auto-launch on double-click (handled by parent)."""
        self._on_select(name)

    # ------------------------------------------------------------------
    # Public API (unchanged)
    # ------------------------------------------------------------------
    def get_selected_project(self):
        return self.selected_project

    def get_selected_project_path(self):
        if self.selected_project:
            return self.db.get_project_path(self.selected_project)
        return None