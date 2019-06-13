import os
import subprocess
from pathlib import Path

from reprobench.tools.executable import ExecutableTool
from reprobench.utils import download_file


DIR = os.path.dirname(__file__)


class MySudokuSolver(ExecutableTool):
    name = "My Sudoku Solver"

    # TODO: change this to your solver executable
    path = os.path.join(DIR, "Sudoku")

    @classmethod
    def setup(cls):
        super().setup()
        # # TODO: add setup as needed (compiling etc.)
        # # for example you can use subprocess to execute GNU Make:s
        # subprocess.run(["make"], cwd=DIR)
        subprocess.run(["make"])

    def get_cmdline(self):
        # TODO: change how your solver is executed against a task.
        # - `self.path` is as defined above
        # - `self.task` contains the path for the sudoku instance
        # - `self.parameters` contains the parameter for the run,
        #   in this case this is the `solver` parameter, whose value
        #   is either `riss` or `glucose`.

        solver = self.parameters.get("Sudoku")
        return [self.path, self.task, solver]

    @classmethod
    def is_ready(cls):
        """This checks if your solver is ready.
        If it returns false, the `setup()` method is executed
        """

        if not super().is_ready():
            return False

        # TODO: maybe check if your solver has already been compiled
        #       by checking the target executable?
        return Path(cls.path).is_file()
