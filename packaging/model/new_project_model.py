class NewProjectModel:
    def __init__(self):
        self.name = ""
        self.path = ""

    def is_valid(self):
        return bool(self.name.strip()) and bool(self.path)

    def set_path(self, path):
        self.path = path