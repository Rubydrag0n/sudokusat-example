#pragma once
#include <string>
#include <vector>
#include <fstream>

class Sudoku
{
public:
	explicit Sudoku(std::string path, bool verbose);
	Sudoku(const std::string& solution_path, const std::string& lut_path);
	~Sudoku();

	void init_size();
	void init_matrix();
	void read_sudoku();

	//sets a field and applies its consequences all over the sudoku
	bool set_field(int row_x, int row_y, int number);

	//uses the simple solve functions until they don't do anything anymore
	void simple_solve();

	//solving functions, they return false if they did nothing and true if they found at least one new number
	bool naked_singles();
	bool naked_candidates();
	bool hidden_singles_columns();
	bool hidden_singles_rows();
	bool hidden_singles_section();
	bool pointing_candidates();
	bool box_line_reduction();
	bool x_wing();

	//returns number at position, or -1 if not "sure"
	int get_number_at_position(int x, int y);

	//prints the current sudoku
	void print();
	void print_out(std::string path);

	//returns the number of known numbers in the sudoku
	//also detects potential insolubility
	int get_solved_fields();

	//clause generator helper functions
	int get_atom_number(int x, int y, int n) const;
	int get_luted_atom_number(int x, int y, int n) const;
	int get_unused_atom();

	//at-most-one-encoding helper functions, they return the number of clauses they created
	int encode_at_most_one(std::vector<int>* numbers);
	int naive_encode_at_most_one(std::vector<int>* numbers);	
	int commander_encode_binary(std::vector<int>* numbers, int* commander);
	int commander_encode(std::vector<int>* numbers);

	//reverse clause generator helper functions
	void get_position(int atom, int* x, int* y, int* n) const;

	//calls all the other clause generating functions
	void generate_all_clauses();

	//clause functions, add clauses
	int add_single_cell_definedness_clauses();			//generates definedness constraints for cells
	int add_single_cell_uniqueness_clauses();			//generates uniqueness constraints for cells

	int add_row_definedness_clauses();					//generates definedness constraints for the rows
	int add_row_uniqueness_clauses();					//generates uniqueness constraints for the rows

	int add_column_definedness_clauses();				//generates definedness constraints for the columns
	int add_column_uniqueness_clauses();				//generates uniqueness constraints for the columns

	int add_section_definedness_clauses();				//generates definedness constraints for the sections
	int add_section_uniqueness_clauses();				//generates uniqueness constraints for the sections

	//functions for the lookup table
	void create_lut();
	void read_lut(const std::string& path);
	void write_lut(const std::string& path) const;

	//writes the clauses out to a file
	void write_clause(std::vector<int>* clause);
	void write_clauses();

	//reads the output of the sat solver and shows the finished sudoku
	void read_solution(const std::string& path);

	int get_size() const;
	int get_n() const;
	bool is_solvable() const;
	int get_ce_size() const;
	int get_number_of_clauses() const;

	void get_unsolvable_cell(int* x, int* y) const;

private:
	
	std::vector<std::vector<std::vector<bool>>> mSudoku_matrix;	//saves which numbers are possible for each field in the sudoku
	std::vector<std::vector<bool>> mFixed_cell;
	int mSize{};												//the size, for a 3-sudoku this will be 9, etc.
	int mN{};													//the n of the sudoku, 3-sudoku -> 3
	std::string mPath;											//path to the sudoku

	//temp file for writing clauses initially
	std::ofstream mClauses_temp_file;
	std::string mTemp_filename;

	//output file for the finished cnf file
	std::string mClauses_output_filename;

	int mNumber_of_clauses = 0;

	int mCommander_encoding_size = 0;

	//lookup table for compressing number of atoms without losing reconstructability
	//this is an array -> at index of atom is its actual counterpart number
	std::vector<int> mLut;

	//the lut stored in reverse, when reading in a solution
	std::vector<int> mRead_lut;

	int mNumber_of_atoms{};
	int mExtra_atom_number;		//used for the extra atoms for the commander encoding

	bool mSolvable = true;

	int mUnsolvable_cell_x = -1;
	int mUnsolvable_cell_y = -1;

	bool mVerbose = false;
};

struct x_wing_type {
	int pos1;
	int pos2;
	int pos3;
};

void benchmark_sudokus(std::string path, std::string solver, std::string output_path);
void solve_sudoku(std::string path, std::string solver, std::string outputfile = "", bool verbose = false, bool omit_output = false);
int system_call(std::string command);

void signal_handler(int signum);
