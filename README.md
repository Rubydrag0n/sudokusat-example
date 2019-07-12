# Sudoku SAT

## Usage

In /my_solver execute `$ make` to compile the solver.

The resulting Sudoku executable is used as follows:
```sh
$ ./Sudoku solve [input file] [solver]
```

The program only outputs the solved Sudoku and the time taken, unless there's an error. It also produces a cnf file "clauses_out.cnf" which is the input for the sat-solver and a file "model.txt" which is the output of the sat-solver.

You can use option '-v' to get verbose output.

Tested solvers are currently only clasp. Other solvers theoretically work too, but for example glucose doesn't output the solution on std::out which is currently the only way my solver accepts the answer.

## How the encoder works

### Reading

The encoder first reads in the unsolved Sudoku. The datastructure to store the Sudoku internally is a 3-dimensional matrix, storing  for every cell which numbers are still possible. That way while reading the Sudoku impossible numbers can already be eliminated from other cells.

### Preprocessing

Before actually encoding the Sudoku into CNF, the encoder uses easy strategies to eliminate more possible numbers and find more fixed cells. These are 

* Naked Singles - if only one number is possible in a given cell, that number can be fixed in that cell
* Hidden Singles - if a number only appears once in a line, column or box, that number can be fixed to that cell
* Intersection Removal - See http://www.sudokuwiki.org/Intersection_Removal

These strategies are applied until they don't change anything anymore.

### Encoding

When the preprocessing finishes the Sudoku is encoded into CNF. A variation of the extended encoding (encoding definedness and uniqueness clauses for cells/lines/columns/boxes) is used. Since every entry in the previously mentioned matrix corresponds to one literal in the CNF and every entry in the matrix that equals false can't be part of the solution. Thus while encoding those can be omitted, reducing the size of the CNF drastically.

Furthermore the Commander Encoding is used for all the at-most-once constraints, reducing the size of the CNF again.

### Solving and Output

The finished CNF is used as input for the SAT-Solver. The result is read in and then written to a file.

## To Do

* Make the program nicer to use from the commandline, make things like the commander encoding size or whether to use certain parts of the preprocessing changeable through options.
* Support more solvers

## Sources

Commander Encoding: https://www.cs.cmu.edu/~wklieber/papers/2007_efficient-cnf-encoding-for-selecting-1.pdf

Preprocessing Strategies: http://www.sudokuwiki.org/Getting_Started

Basic and Extended Encoding: https://pdfs.semanticscholar.org/3d74/f5201b30772620015b8e13f4da68ea559dfe.pdf

Fixed Cell Encoding: https://www.cs.cmu.edu/~hjain/papers/sudoku-as-SAT.pdf

More ideas: https://easychair.org/publications/open/VF3m
