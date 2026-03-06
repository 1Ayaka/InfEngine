from PySide6.QtWidgets import QFileDialog


class NewProjectViewModel:
    def __init__(self, model):
        self.model = model

    def set_name(self, name: str):
        self.model.name = name.strip()

    def choose_path(self, parent):
        folder = QFileDialog.getExistingDirectory(parent, "Select Project Location")
        if folder:
            self.model.path = folder
        return folder

    def get_data(self):
        return self.model.name, self.model.path

    def is_valid(self):
        return self.model.is_valid()

    def set_path(self, path):
        self.model.set_path(path)