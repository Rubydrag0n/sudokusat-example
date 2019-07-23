// Sudoku.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "Sudoku.h"
#include <fstream>
#include <utility>
#include <iostream>
#include <sstream>
#include <complex>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>

const int MAX_PRINT_SIZE = 36;
const int HEADER_LINES = 4;

const bool COMMANDER_ENCODING = true; 				//toggles if the advanced "commander encoding" is used for the "at most one"-clauses
const bool COMMANDER_ENCODING_BINARY = false;		//toggles if the binary tree commander encoding or the new one is used
const int COMMANDER_ENCODING_SIZES[] = {0, 0, 0, 3, 4, 5, 4, 6, 4, 4, 3, 4, 6, 4, 4, 3};
unsigned COMMANDER_ENCODING_MAX_SIZE = 5;			//defines the maximum group size for clauses when using the commander encoding
const bool ENCODE_EXTRA_COMMANDERS = false;			//toggles if extra commanders will be generated for single variables

const bool SIMPLE_SOLVING_ENABLED = true;			//toggles whether the encoder tries to use simple rules on the sudoku before encoding
const bool POINTING_CANDIDATES_ENABLED = true;		//toggles advanced rule "intersetion removal" for the simple solve part
const bool BOX_LINE_REDUCTION_ENABLED = true;		//toggles advanced rule "box line reduction" for the simple solve part

//disabled since it rarely actually finds anything new
const bool X_WING_ENABLED = false;					//toggles advanced rule "x_wing" for the simple solve part

pid_t command_pid = -1;

int main(const int argc, char** argv)
{
	auto start = std::chrono::steady_clock::now();

	//register signal handler for all possible signals
	signal(SIGABRT, signal_handler);
	signal(SIGFPE, signal_handler);
	signal(SIGILL, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);

    //std::cout << "Sudoku Solver by Anton Reinhard!" << std::endl;

	if (argc == 1)
	{
		std::cout << "Usage: ./Sudoku [command] [options]" << std::endl;
		return 0;
	}

	
	//collect commandline options
	std::vector<char> options;

	for (auto i = 2; i < argc; ++i) {
		if (argv[i][0] == '-') {
			options.push_back(argv[i][1]);
		}
	}

	auto use_standard_size = false;
	auto verbose = false;
	auto omit_output = false;

	for (auto option : options) {
		if (option == 'v') {
			verbose = true;
		} else if (option == 'd') {
			omit_output = true;
		}
		else {
			std::cout << "Encountered unknown option \"" << option << "\", ignoring." << std::endl;
		}
	}

	std::string command = argv[1];

	if (command == "-h" || command == "help")
	{
		std::cout << "Usage: ./Sudoku [command] [arguments] [options]" << std::endl;
		std::cout << "Possible commands are: solve, benchmark" << std::endl;
	}
	else if (command == "solve")
	{
		if (argc == 2) {
			std::cout << "Usage: ./Sudoku solve [path] [sat-solver] [options]" << std::endl;
			return 0;
		}
		
		const auto path_index = 2;
		const auto solver_index = 3;

		const std::string path = argv[path_index];
		const std::string solver = argv[solver_index];

		if (solver != "clasp")
		{
			std::cout << "Solvers other than clasp are not supported right now." << std::endl;
			return 0;
		}

		solve_sudoku(path, solver, "", verbose, omit_output);
	}
	else if (command == "benchmark")
	{
		if (argc <= 4) 
		{
			std::cout << "Too few arguments for benchmark! Usage: ./Sudoku benchmark [folder] [solver] [output file] [options]" << std::endl;
			return -1;
		}

		std::string folder = argv[2];
		std::string solver = argv[3];
		std::string output_file = argv[4];

		benchmark_sudokus(folder, solver, output_file);
	}
	else 
	{
		std::cout << "Unknown command \"" << command << "\"" << std::endl;
		std::cout << "Possible commands are: solve, benchmark" << std::endl;
		return 0;
	}

	auto end = std::chrono::steady_clock::now();

	auto diff = end - start;

	std::cout << "Execution took " << std::chrono::duration_cast<std::chrono::milliseconds>(diff).count() / 1000. << " seconds" << std::endl;

	return 0;
}

void benchmark_sudokus(std::string path, std::string solver, std::string output_path)
{
	std::cout << "Benchmarking at " << path << " with solver " << solver << "..." << std::endl;

	auto n = 3;
	auto count = 0;

	std::ofstream benchmark(output_path);

	benchmark << "Sudoku,Size,CE-Size,No. Atoms,No. Clauses,Seconds\n";

	benchmark.close();

	do 
	{
		std::stringstream full_path_ss;
		std::string full_path;
		std::ifstream file;
		++count;

		full_path_ss << path << "/extable" << (n*n) << "-" << count << ".txt";
		full_path_ss >> full_path;

		file.open(full_path);

		if (!file.is_open())	//if we couldn't open the file
		{
			++n;
			count = 0;
		} else {
			std::cout << "Solving Sudoku at " << full_path << std::endl;

			solve_sudoku(full_path, solver, output_path, false, true);

			//std::cout << "Done solving Sudoku at " << full_path << std::endl;
		}
	} while (n <= 15);
}

void solve_sudoku(std::string path, std::string solver, std::string outputfile, bool verbose, bool omit_output)
{
	//record time taken
	auto sudoku_start = std::chrono::steady_clock::now();
	std::string cnf_filename = "clauses_out.cnf";
	std::string solution_filename = "model.txt";

	if (verbose) std::cout << "Solving Sudoku at \"" << path << "\"" << std::endl;

	Sudoku sudoku(path, verbose);

	const auto size = sudoku.get_size();

	if (verbose) 
	{
		if (size <= MAX_PRINT_SIZE)
			sudoku.print();
		else
			std::cout << "Sudoku too big to print..." << std::endl;
	}

	const auto known_numbers_before = sudoku.get_solved_fields();

	if (!sudoku.is_solvable())
	{
		std::cerr << "This Sudoku is unsolvable!" << std::endl;
		int x, y;
		sudoku.get_unsolvable_cell(&x, &y);
		std::cerr << "There's no possible number for cell at position " << ++x << ", " << ++y << "." << std::endl;
		std::cerr << "Exiting..." << std::endl;
		return;
	}

	if (verbose) std::cout << known_numbers_before << " of " << size * size << " cells are filled." << std::endl;

	if (SIMPLE_SOLVING_ENABLED)
		sudoku.simple_solve();

	const auto known_numbers_after = sudoku.get_solved_fields();

	if (verbose) std::cout << "Simple Solve found " << known_numbers_after - known_numbers_before << " new numbers." << std::endl;
	if (verbose) std::cout << "Now " << known_numbers_after << " of " << size * size << " cells are filled." << std::endl;

	if (size <= MAX_PRINT_SIZE && verbose)
		sudoku.print();

	sudoku.create_lut();

	sudoku.generate_all_clauses();

	sudoku.write_clauses();

	//now execute the solver

	if (verbose) std::cout << "Using solver " << solver << "..." << std::endl;

	std::stringstream syscall;
	syscall << solver;
	syscall << " " << cnf_filename << " > " << solution_filename;
	//I'm confused about if it's better to search for all models or not
	
	if (verbose) std::cout << "Executing " << syscall.str() << "..." << std::endl;

	auto time_pre_syscall = std::chrono::steady_clock::now() - sudoku_start;

	int ret = system_call(syscall.str());
	if (ret == -1) {
		if (verbose) std::cout << "Couldn't execute solver, exiting..." << std::endl;
		return;
	}

	auto time_after_syscall_start = std::chrono::steady_clock::now();

	if (verbose) std::cout << "Reading solution... " << std::endl;

	sudoku.read_solution(solution_filename);

	if (!omit_output)
		sudoku.print();

	auto sudoku_end = std::chrono::steady_clock::now();
	auto sudoku_time = sudoku_end - sudoku_start;

	auto time_encoding_total = time_pre_syscall + (sudoku_end - time_after_syscall_start);

	std::cout << "Encoding took " << std::chrono::duration_cast<std::chrono::milliseconds>(time_encoding_total).count() / 1000. << " seconds" << std::endl;

	auto time = std::chrono::duration_cast<std::chrono::milliseconds>(sudoku_time).count() / 1000.;

	if (outputfile != "") {
		std::ofstream benchmark_file(outputfile, std::ofstream::out | std::ofstream::app);
		benchmark_file << path << "," << sudoku.get_size() << "," << sudoku.get_ce_size() << "," << sudoku.get_unused_atom() - 1 
			  << "," << sudoku.get_number_of_clauses() << "," << time << "\n" << std::flush;

		benchmark_file.close();
	}
}

//system call using fork to be able to kill the command
int system_call(std::string command)
{
	command_pid = fork();

	if (command_pid < 0)
	{
		std::cout << "Failed to fork!" << std::endl;
		return -1;
	}

	if (command_pid == 0) 
	{
		//child
		//execute command here
		system(command.c_str());
		exit(1);
	}
	else
	{
		//parent
		int status;
		wait(&status);
	}

	//not executing any command anymore...
	command_pid = -1;
	return 1;
}

void signal_handler(int signum)
{
	if (command_pid != -1)
	{
		kill(command_pid, signum);
	}

	exit(signum);
}

//adapted from https://www.geeksforgeeks.org/extract-integers-string-c/
int get_first_integer(const std::string& str)
{
	std::stringstream ss;

	ss << str;

	/* Running loop till the end of the stream */
	std::string temp;
	int found;
	while (!ss.eof()) {

		/* extracting word by word from stream */
		ss >> temp;

		/* Checking the given word is integer or not */
		if (std::stringstream(temp) >> found)
			return found;

		/* To save from space at the end of string */
		temp = "";
	}

	return -1;
}

Sudoku::Sudoku(std::string path, bool verbose): mPath(std::move(path)), mTemp_filename("temp_clauses.txt"), 
												mClauses_output_filename("clauses_out.cnf"), mVerbose(verbose)
{
	this->init_size();

	mExtra_atom_number = -1;			//initialize to invalid first, is initialized when requested

	mLut.resize(mSize * mSize * mSize + 1, 0);

	if (mVerbose) std::cout << "Sudoku of type " << mSize << "x" << mSize << "." << std::endl;

	if (mN >= 3 && mN <= 15 && COMMANDER_ENCODING) {
		mCommander_encoding_size = COMMANDER_ENCODING_SIZES[mN];
		if (mVerbose) std::cout << "Using commander encoding with max group size " << mCommander_encoding_size << "." << std::endl;
	}

	init_matrix();

	this->read_sudoku();
}

Sudoku::Sudoku(const std::string& solution_path, const std::string& lut_path)
{
	//read the finished clauses
	this->read_lut(lut_path);

	mN = int(std::sqrt(mSize));

	if (mVerbose) std::cout << "Size is " << mSize << std::endl;

	this->init_matrix();

	this->read_solution(solution_path);

	this->print_out("solved.txt");
}

Sudoku::~Sudoku() = default;

void Sudoku::init_size()
{
	std::ifstream file(mPath);

	if (!file.is_open()) return;

	std::string content;

	//read first header line to check format
	std::getline(file, content);
	if (!content.rfind("experiment:", 0)) {
		//standard test sudokus, continue to read rest of header lines
		for (auto i = 0; i < HEADER_LINES - 1; ++i) {
			std::getline(file, content);
		}

		const auto pos = content.find('x');
		content.erase(pos);					//delete part after the x so only one integer can be found

		mSize = get_first_integer(content);

	} else {
		//format from sudoku reader -> only two header lines
		std::getline(file, content);

		//this can only be a 3-Sudoku, the reader only reads 3-Sudokus
		mSize = 9;
	}

	mN = int(std::sqrt(mSize));

	file.close();
}

void Sudoku::init_matrix()
{
	if (mVerbose) std::cout << "Initializing matrix... " << std::flush;

	//initialize the sudoku matrix with all ones
	this->mSudoku_matrix.resize(mSize);
	this->mFixed_cell.resize(mSize);
	for (auto i = 0; i < mSize; ++i)
	{
		this->mSudoku_matrix[i].resize(mSize);
		this->mFixed_cell[i].resize(mSize, false);
		for (auto j = 0; j < mSize; ++j)
		{
			this->mSudoku_matrix[i][j].resize(mSize, true);
		}
	}
	if (mVerbose) std::cout << "Done!" << std::endl;
}

void Sudoku::read_sudoku()
{
	std::ifstream file(mPath);
	std::string content;

	//read first header line to check format
	std::getline(file, content);
	if (content.rfind("experiment:", 0) == 0) {
		//standard test sudokus, continue to read rest of header lines
		for (auto i = 0; i < HEADER_LINES - 1; ++i) {
			std::getline(file, content);
		}
	} 
	else if (content.rfind("+", 0) == 0) {
		//only sudoku in the file, this is delimiter line, do nothing
	}
	else {
		//format from sudoku reader -> only two header lines
		std::getline(file, content);
	}

	std::string empty_field;
	for (auto i = 1; i <= mSize; i *= 10) empty_field += "_";

	//reading the sudoku
	for (auto y = 0; y < mSize; ++y)
	{
		if (mVerbose) std::cout << "\rReading Sudoku... line " << y+1 << "/" << mSize << " " << std::flush;
		std::getline(file, content);
		if (content.find('+') != std::string::npos) std::getline(file, content);		//if this is a delimiter line, skip it

		std::stringstream stream;
		stream << content;
		std::string number;
		for (auto x = 0; x < mSize; ++x)
		{
			stream >> number;
			if (number == "|") stream >> number;	//if this is a delimiter char, skip it
			if (number != empty_field)				//if this field is empty, skip it
			{										//otherwise read number and fill field
				int field_number;
				std::stringstream(number) >> field_number;
				if (field_number)					//a number may be 0 if there's actually no number there (different input format)
					this->set_field(x, y, field_number - 1);
			}
		}
	}

	if (mVerbose) std::cout << "Done!" << std::endl;
}

bool Sudoku::set_field(const int row_x, const int row_y, const int number)
{
	//checking parameters
	if (number >= mSize || number < 0) return false;
	if (row_x >= mSize || row_x < 0) return false;
	if (row_y >= mSize || row_y < 0) return false;
	if (mFixed_cell[row_x][row_y]) return false;		//if this cell is already set

	mFixed_cell[row_x][row_y] = true;

	for (auto i = 0; i < mSize; ++i)
	{
		mSudoku_matrix[row_x][row_y][i] = number == i;		//set this field

		if (i != row_x)	mSudoku_matrix[i][row_y][number] = false;	//set the column
		if (i != row_y) mSudoku_matrix[row_x][i][number] = false;	//set the row
	}

	//set the section
	const auto section_x = row_x / mN;
	const auto section_y = row_y / mN;

	for (auto x_i = section_x * mN; x_i < (section_x+1)*mN; ++x_i)
	{
		for (auto y_i = section_y * mN; y_i < (section_y + 1)*mN; ++y_i)
		{
			if (row_x != x_i && row_y != y_i)	//don't for the field we're actually setting
			{
				mSudoku_matrix[x_i][y_i][number] = false;
			}
		}
	}

	return true;
}

void Sudoku::simple_solve()
{
	//use the simple_solve functions as long as they still do something

	auto counter = 0;
	bool keep_going;
	do
	{
		++counter;
		if (mVerbose) std::cout << "\rSimple-Solving in iteration " << counter << "... " << std::flush;
		keep_going = false;

		if (naked_singles()) keep_going = true;

		if (naked_candidates()) keep_going = true;

		if (hidden_singles_columns()) keep_going = true;

		if (hidden_singles_rows()) keep_going = true;

		if (hidden_singles_section()) keep_going = true;

		//intersection removal is more expensive, so try the other stuff first
		if (keep_going) continue;

		if (POINTING_CANDIDATES_ENABLED && pointing_candidates()) keep_going = true;

		if (BOX_LINE_REDUCTION_ENABLED && box_line_reduction()) keep_going = true;

		//x_wings are very expensive, so try the other stuff first
		if (keep_going) continue;

		if (X_WING_ENABLED && x_wing()) keep_going = true;

	} while (keep_going);

	if (mVerbose) std::cout << "Done!" << std::endl;
}

bool Sudoku::naked_singles()
{
	auto result = false;
	for (auto x = 0; x < mSize; ++x) {
		for (auto y = 0; y < mSize; ++y) {
			auto n = get_number_at_position(x, y);
			if (n != -1 && !mFixed_cell[x][y])		//if there's a number there but not entered in the fixed cell matrix
			{
				set_field(x, y, n);
				result = true;
			}
		}
	}
	return result;
}

bool Sudoku::naked_candidates()
{
	auto result = false;

	for (auto x = 0; x < mSize; ++x) {
		for (auto y = 0; y < mSize; ++y) {
			//skip already fixed cells
			if (mFixed_cell[x][y]) continue;

			//copy this cell
			auto numbers = mSudoku_matrix[x][y];

			auto m = 0;
			for (auto n = 0; n < mSize; ++n) m += numbers[n];

			std::vector<int> found{x};

			//go through same column to check for cells with exactly same numbers in them
			for (auto x_i = x+1; x_i < mSize; ++x_i) {
				if (numbers == mSudoku_matrix[x_i][y]) {
					found.push_back(x_i);
				}
			}

			if (found.size() == m) {
				//found enough cells -> delete all other occurrences of any numbers in the column
				for (auto x_i = 0; x_i < mSize; ++x_i) {
					if (!found.size() || found[0] == x_i) {
						//ignore this one
						if (found.size()) found.erase(found.begin());
					} else {
						for (auto n_i = 0; n_i < mSize; ++n_i) {
							if (numbers[n_i]) {
								if (!result && mSudoku_matrix[x_i][y][n_i]) result = true;
								mSudoku_matrix[x_i][y][n_i] = false;
							}
						}
					}
				}
			}

			found = {y};

			//go through same row to check for cells with exactly same numbers in them
			for (auto y_i = y+1; y_i < mSize; ++y_i) {
				if (numbers == mSudoku_matrix[x][y_i]) {
					found.push_back(y_i);
				}
			}

			if (found.size() == m) {
				//found enough cells -> delete all other occurrences of any numbers in the row
				for (auto y_i = 0; y_i < mSize; ++y_i) {
					if (!found.size() || found[0] == y_i) {
						//ignore this one
						if (found.size()) found.erase(found.begin());
					} else {
						for (auto n_i = 0; n_i < mSize; ++n_i) {
							if (numbers[n_i]) {
								if (!result && mSudoku_matrix[x][y_i][n_i]) result = true;
								mSudoku_matrix[x][y_i][n_i] = false;
							}
						}
					}
				}
			}
		}
	}

	return result;
}

bool Sudoku::hidden_singles_columns()
{
	auto result = false;
	for (auto x = 0; x < mSize; ++x)
	{
		for (auto n = 0; n < mSize; ++n)	//go through all the numbers
		{
			//search if this number only occurs once in this column
			auto y_pos = -1;
			for (auto y = 0; y < mSize; ++y)
			{
				if (mSudoku_matrix[x][y][n])
				{
					if (y_pos == -1) y_pos = y;
					else
					{
						y_pos = -1;
						break;
					}
				}
			}

			//if y_pos is not -1 at this point then n can be entered in the field
			if (y_pos != -1 && get_number_at_position(x, y_pos) != n)
			{
				set_field(x, y_pos, n);
				result = true;
			}
		}
	}
	return result;
}

bool Sudoku::hidden_singles_rows()
{
	auto result = false;

	for (auto y = 0; y < mSize; ++y)
	{
		for (auto n = 0; n < mSize; ++n)	//go through all the numbers
		{
			//search if this number only occurs once in this row
			auto x_pos = -1;
			for (auto x = 0; x < mSize; ++x)
			{
				if (mSudoku_matrix[x][y][n])
				{
					if (x_pos == -1) x_pos = x;
					else
					{
						x_pos = -1;
						break;
					}
				}
			}

			//if x_pos is not -1 at this point then n can be entered in the field
			if (x_pos != -1 && get_number_at_position(x_pos, y) != n)
			{
				set_field(x_pos, y, n);
				result = true;
			}
		}
	}
	return result;
}

bool Sudoku::hidden_singles_section()
{
	auto result = false;

	for (auto section_x = 0; section_x < mN; ++section_x) {
		for (auto section_y = 0; section_y < mN; ++section_y) {
			for (auto n = 0; n < mSize; ++n)	//go through all the numbers
			{
				//search if this number only occurs once in this section
				auto x_pos = -1;
				auto y_pos = -1;

				for (auto x_i = section_x * mN; x_i < (section_x + 1)*mN; ++x_i)
				{
					for (auto y_i = section_y * mN; y_i < (section_y + 1)*mN; ++y_i)
					{
						if (mSudoku_matrix[x_i][y_i][n])
						{
							if (x_pos == -1)
							{
								x_pos = x_i;
								y_pos = y_i;
							}
							else
							{
								x_pos = -1;
								y_pos = -1;
								//break out of both loops
								x_i = mSize;
								y_i = mSize;
							}
						}
					}
				}

				//if x_pos is not -1 at this point then n can be entered in the field
				if (x_pos != -1 && get_number_at_position(x_pos, y_pos) != n)
				{
					set_field(x_pos, y_pos, n);
					result = true;
				}
			}
		}
	}
	return result;
}

bool Sudoku::pointing_candidates()
{
	auto result = false;

	for (auto n = 0; n < mSize; ++n)	//go through all the numbers
	{
		for (auto section_x = 0; section_x < mN; ++section_x) {	//go through all the sections
			for (auto section_y = 0; section_y < mN; ++section_y) {

				std::vector<int> occurrences_x, occurrences_y;

				for (auto x_i = section_x * mN; x_i < (section_x + 1)*mN; ++x_i) {	//go through the section
					for (auto y_i = section_y * mN; y_i < (section_y + 1)*mN; ++y_i) {
						if (!mSudoku_matrix[x_i][y_i][n]) continue;
						occurrences_x.push_back(x_i);		//collect all occurrences of one number in the section
						occurrences_y.push_back(y_i);
					}
				}

				if (occurrences_x.size() <= 1) continue;	//skip if only found once or less -> number is already set

				//1. check align in the box -> "pointing pairs, triplets, etc"
				auto x_aligned = true;
				auto y_aligned = true;
				const auto x_align = occurrences_x[0];
				const auto y_align = occurrences_y[0];

				for (unsigned i = 1; i < occurrences_x.size(); ++i)
				{
					if (occurrences_x[i] != x_align) x_aligned = false;
					if (occurrences_y[i] != y_align) y_aligned = false;
				}

				if (x_aligned)
				{
					//delete all other occurrences of n in this column
					for (auto y = 0; y < mSize; ++y)
					{
						if (y == section_y * mN) {
							y += mN - 1; //skip section we're in
							continue;
						}
						if (!result && mSudoku_matrix[x_align][y][n]) result = true;
						mSudoku_matrix[x_align][y][n] = false;
					}
				}
				if (y_aligned)
				{
					//delete all other occurrences of n in this column
					for (auto x = 0; x < mSize; ++x)
					{
						if (x == section_x * mN) {
							x += mN - 1; //skip section we're in
							continue;
						}
						if (!result && mSudoku_matrix[x][y_align][n]) result = true;
						mSudoku_matrix[x][y_align][n] = false;
					}
				}
			}
		}
	}

	return result;
}

bool Sudoku::box_line_reduction()
{
	auto result = false;

	for (auto n = 0; n < mSize; ++n)	//for all numbers
	{
		for (auto x = 0; x < mSize; ++x)	//for all the columns
		{
			//search for first occurrence of n, then check that all following n are in the same section
			const auto section_x = x / mN;
			auto section_y = -1;
			auto possible = true;
			auto count = 0;
			for (auto y = 0; y < mSize; ++y)
			{
				if (!mSudoku_matrix[x][y][n]) continue;
				if (section_y == -1) section_y = y / mN;
				else if (section_y != y / mN) {
					possible = false;
					break;
				}
				++count;
			}
			if (possible && section_y != -1 && count > 1)	//only if actually all numbers were in one section, and we found more than one number it's worth deleting the potential other n in that section
			{
				for (auto x_i = section_x * mN; x_i < (section_x + 1)*mN; ++x_i) {		//iterate through the single section
					for (auto y_i = section_y * mN; y_i < (section_y + 1)*mN; ++y_i) {
						if (x_i == x) continue;	//don't delete the n on the line we're searching
						if (!result && mSudoku_matrix[x_i][y_i][n]) result = true;
						mSudoku_matrix[x_i][y_i][n] = false;
					}
				}
			}
		}

		for (auto y = 0; y < mSize; ++y)	//for all the rows
		{
			//search for first occurrence of n, then check that all following n are in the same section
			auto section_x = -1;
			const auto section_y = y / mN;
			auto possible = true;
			auto count = 0;
			for (auto x = 0; x < mSize; ++x)
			{
				if (!mSudoku_matrix[x][y][n]) continue;
				if (section_x == -1) section_x = x / mN;
				else if (section_x != x / mN) {
					possible = false;
					break;
				}
				++count;
			}
			if (possible && section_x != -1 && count > 1)	//only if actually all numbers were in one section, and we found more than one number it's worth deleting the potential other n in that section
			{
				for (auto x_i = section_x * mN; x_i < (section_x + 1)*mN; ++x_i) {		//iterate through the single section
					for (auto y_i = section_y * mN; y_i < (section_y + 1)*mN; ++y_i) {
						if (y_i == y) continue;	//don't delete the n on the line we're searching
						if (!result && mSudoku_matrix[x_i][y_i][n]) result = true;
						mSudoku_matrix[x_i][y_i][n] = false;
					}
				}
			}
		}
	}

	return result;
}

bool Sudoku::x_wing()
{
	auto result = false;

	for (auto n = 0; n < mSize; ++n)	//for all numbers
	{
		std::vector<x_wing_type> x_wing_candidates;

		for (auto x = 0; x < mSize; ++x)	//for all the columns
		{
			//need to find the same number exactly twice in this column and save the y coordinates of them
			int n1_y = -1;
			int n2_y = -1;
			for (auto y = 0; y < mSize; ++y) {
				if (mSudoku_matrix[x][y][n]) {
					if (n1_y == -1) n1_y = y;
					else if (n2_y == -1) n2_y = y;
					else { //found too many n
						n1_y = -1;
						n2_y = -1;
						break;
					}
				}
			}

			//check if exactly two n were found
			if (n1_y != -1 && n2_y != -1) {
				//check if there already is a x_wing possible
				for (auto candidate : x_wing_candidates) {
					if (candidate.pos1 == n1_y && candidate.pos2 == n2_y) {
						//found x_wing! -> eliminate all n in rows y1 and y2
						for (auto x_remove = 0; x_remove < mSize; ++x_remove) {
							if (x_remove == x || x_remove == candidate.pos3) continue;
							if (!result && mSudoku_matrix[x_remove][n1_y][n]) result = true;	//notice if anything is actually being done
							if (!result && mSudoku_matrix[x_remove][n2_y][n]) result = true;
							mSudoku_matrix[x_remove][n1_y][n] = false;
							mSudoku_matrix[x_remove][n2_y][n] = false;
						}
					}
				}

				x_wing_candidates.push_back({n1_y, n2_y, x});		//save for later;
			}
		}

		x_wing_candidates.clear();

		for (auto y = 0; y < mSize; ++y)	//for all the rows
		{
			//need to find the same number exactly twice in this row and save the x coordinates of them
			int n1_x = -1;
			int n2_x = -1;
			for (auto x = 0; x < mSize; ++x) {
				if (mSudoku_matrix[x][y][n]) {
					if (n1_x == -1) n1_x = x;
					else if (n2_x == -1) n2_x = x;
					else { //found too many n
						n1_x = -1;
						n2_x = -1;
						break;
					}
				}
			}

			//check if exactly two n were found
			if (n1_x != -1 && n2_x != -1) {
				//check if there already is a x_wing possible
				for (auto candidate : x_wing_candidates) {
					if (candidate.pos1 == n1_x && candidate.pos2 == n2_x) {
						//found x_wing! -> eliminate all n in columns x1 and x2
						for (auto y_remove = 0; y_remove < mSize; ++y_remove) {
							if (y_remove == y || y_remove == candidate.pos3) continue;
							if (!result && mSudoku_matrix[n1_x][y_remove][n]) result = true;	//notice if anything is actually being done
							if (!result && mSudoku_matrix[n2_x][y_remove][n]) result = true;
							mSudoku_matrix[n1_x][y_remove][n] = false;
							mSudoku_matrix[n2_x][y_remove][n] = false;
						}
					}
				}

				x_wing_candidates.push_back({n1_x, n2_x, y});		//save for later;
			}
		}
	}

	return result;
}

int Sudoku::get_number_at_position(const int x, const int y)
{
	if (x >= mSize || x < 0) return -1;
	if (y >= mSize || y < 0) return -1;

	auto result = -1;
	for (auto i = 0; i < mSize; ++i)
	{
		if (mSudoku_matrix[x][y][i]) {
			if (result == -1) result = i;
			else return -1;					//if there are multiple numbers possible here then we're not sure what number is here
		}
		if (i == mSize - 1 && result == -1 && mSolvable) {
			mUnsolvable_cell_x = x;
			mUnsolvable_cell_y = y;
			mSolvable = false;				//this can't happen on a solvable sudoku
		}
	}
	return result;
}

void Sudoku::print()
{
	auto solved_fields = 0;

	auto number_length = 0;
	std::string empty_field;
	for (auto i = 1; i <= mSize; i *= 10)
	{
		++number_length;		
		empty_field += '_';
	}
	empty_field += ' ';

	std::string limit_line;
	for (auto x = 0; x < mN; ++x)
	{
		limit_line += '+';
		for (auto i = 0; i < (number_length + 1) * mN + 1; ++i) limit_line += '-';
	}
	limit_line += '+';

	for (auto y = 0; y < mSize; ++y)
	{
		if (y % mN == 0)
		{
			//draw limiter line
			std::cout << limit_line << "\n";
		}

		for (auto x = 0; x < mSize; ++x)
		{
			if (x % mN == 0)
			{
				//draw limiter char
				std::cout << "| ";
			}

			auto number = get_number_at_position(x, y);
			if (number == -1) std::cout << empty_field;
			else {
				number++;
				auto n = 0;
				for (auto i = 1; i <= number; i *= 10) ++n;
				for (auto i = n; i < number_length; ++i) std::cout << ' ';
				std::cout << number << ' ';
				++solved_fields;
			}
		}
		std::cout << "|\n";
	}
	std::cout << limit_line << std::endl;
	//std::cout << "Solved fields: " << solved_fields << "/" << mSize * mSize << std::endl;
}

void Sudoku::print_out(std::string path)
{
	if (mVerbose) std::cout << "Printing solved sudoku to " << path << "... ";

	std::ofstream file(path);

	auto solved_fields = 0;

	auto number_length = 0;
	std::string empty_field;
	for (auto i = 1; i <= mSize; i *= 10)
	{
		++number_length;		
		empty_field += '_';
	}
	empty_field += ' ';

	std::string limit_line;
	for (auto x = 0; x < mN; ++x)
	{
		limit_line += '+';
		for (auto i = 0; i < (number_length + 1) * mN + 1; ++i) limit_line += '-';
	}
	limit_line += '+';

	for (auto y = 0; y < mSize; ++y)
	{
		if (y % mN == 0)
		{
			//draw limiter line
			file << limit_line << "\n";
		}

		for (auto x = 0; x < mSize; ++x)
		{
			if (x % mN == 0)
			{
				//draw limiter char
				file << "| ";
			}

			auto number = get_number_at_position(x, y);
			if (number == -1) file << empty_field;
			else {
				number++;
				auto n = 0;
				for (auto i = 1; i <= number; i *= 10) ++n;
				for (auto i = n; i < number_length; ++i) file << ' ';
				file << number << ' ';
				++solved_fields;
			}
		}
		file << "|\n";
	}
	file << limit_line << std::endl;

	if (mVerbose) std::cout << "Done!" << std::endl;
}

int Sudoku::get_solved_fields()
{
	auto solved_fields = 0;

	for (auto y = 0; y < mSize; ++y)
		for (auto x = 0; x < mSize; ++x)
			if (get_number_at_position(x, y) != -1) 
				++solved_fields;

	return solved_fields;
}

int Sudoku::get_atom_number(const int x, const int y, const int n) const
{
	return x * mSize * mSize + y * mSize + n + 1;	//+1 so 0 is impossible
}

int Sudoku::get_luted_atom_number(const int x, const int y, const int n) const
{
	return mLut.at(get_atom_number(x, y, n));
}

int Sudoku::get_unused_atom()
{
	return mExtra_atom_number++;	//return, then +1
}

int Sudoku::encode_at_most_one(std::vector<int>* numbers)
{
	if (!ENCODE_EXTRA_COMMANDERS && numbers->size() <= 1) return 0;			//don't need to generate anything
	if (COMMANDER_ENCODING) {
		if (COMMANDER_ENCODING_BINARY) {
			int commander;
			return  commander_encode_binary(numbers, &commander);
		} else {
			return commander_encode(numbers);
		}
	} else {
		return naive_encode_at_most_one(numbers);
	}
}

int Sudoku::naive_encode_at_most_one(std::vector<int>* numbers)
{
	//numbers contains positive literals
	auto generated_clauses = 0;
	const auto numbers_amount = numbers->size();
	for (unsigned n = 0; n < numbers_amount; ++n) {
		for (auto m = n + 1; m < numbers_amount; ++m) {
			std::vector<int> clause;
			clause.push_back(-(*numbers)[n]);
			clause.push_back(-(*numbers)[m]);
			clause.push_back(0);
			write_clause(&clause);
			++generated_clauses;
		}
	}
	return generated_clauses;
}

int Sudoku::commander_encode_binary(std::vector<int>* numbers, int* commander)
{
	auto generated_clauses = 0;

	//generate a commander for this group
	*commander = get_unused_atom();

	auto size = numbers->size();
	if (size <= mCommander_encoding_size)
	{	
		//stop case -> there are few enough to just naive encode them

		//generate the commander group clauses
		for (unsigned i = 0; i < size; ++i) {
			std::vector<int> clause;
			clause.push_back(*commander);
			clause.push_back(-(*numbers)[i]);
			clause.push_back(0);
			write_clause(&clause);
			++generated_clauses;
		}

		//generate the naive encoding
		return generated_clauses + naive_encode_at_most_one(numbers);
	}

	//otherwise -> divide into two halves

	std::size_t const half_size = size / 2;
	std::vector<int> group_a(numbers->begin(), numbers->begin() + half_size);
	std::vector<int> group_b(numbers->begin() + half_size, numbers->end());

	//generate group_a commander clauses
	int commander_a, commander_b;
	generated_clauses += commander_encode_binary(&group_a, &commander_a);
	generated_clauses += commander_encode_binary(&group_b, &commander_b);

	std::vector<int> commanders;
	commanders.push_back(commander_a);
	commanders.push_back(commander_b);

	//at most one of these commanders may be true
	generated_clauses += naive_encode_at_most_one(&commanders);

	//connect to new commander
	std::vector<int> commander_a_connect_clause, commander_b_connect_clause;
	commander_a_connect_clause.push_back(*commander);
	commander_a_connect_clause.push_back(-commander_a);
	commander_a_connect_clause.push_back(0);
	commander_b_connect_clause.push_back(*commander);
	commander_b_connect_clause.push_back(-commander_b);
	commander_b_connect_clause.push_back(0);
	write_clause(&commander_a_connect_clause);
	write_clause(&commander_b_connect_clause);

	generated_clauses += 2;

	return generated_clauses;
}

int Sudoku::commander_encode(std::vector<int>* numbers)
{
	auto generated_clauses = 0;

	std::vector<int> commanders;
	auto number_count = numbers->size();

	//stop case
	if (number_count <= 1) return generated_clauses;

	//divide into subgroups of given size
	for (unsigned i = 0; i < number_count; i += mCommander_encoding_size) {
		std::vector<int> subgroup;
		for (unsigned j = i; j < i + mCommander_encoding_size && j < number_count; ++j) {
			subgroup.push_back(numbers->at(j));
		}

		//encode subgroups naively
		generated_clauses += naive_encode_at_most_one(&subgroup);

		//generate the connection from commander to group
		auto commander = get_unused_atom();
		commanders.push_back(commander);
		for (unsigned j = 0; j < subgroup.size(); ++j) {
			std::vector<int> clause;
			clause.push_back(commander);
			clause.push_back(-subgroup[j]);
			clause.push_back(0);
			write_clause(&clause);
			++generated_clauses;
		}
	}

	//commander encode the commanders now
	generated_clauses += commander_encode(&commanders);

	return generated_clauses;
}

void Sudoku::get_position(int atom, int* x, int* y, int* n) const
{
	atom = mRead_lut.at(atom) - 1;
	*n = atom % mSize;
	atom = atom / mSize;
	*y = atom % mSize;
	atom = atom / mSize;
	*x = atom;
}

void Sudoku::generate_all_clauses()
{
	if (mVerbose) std::cout << "Generating clauses...\n";

	mClauses_temp_file.open(mTemp_filename);
	if (!mClauses_temp_file.is_open())
	{
		std::cerr << "Couldn't open temporary clause file \"" << mTemp_filename << "\". Exiting..." << std::endl;		
	}

	auto total_clauses = 0;

	total_clauses += this->add_single_cell_definedness_clauses();
	total_clauses += this->add_single_cell_uniqueness_clauses();

	total_clauses += this->add_row_definedness_clauses();
	total_clauses += this->add_row_uniqueness_clauses();

	total_clauses += this->add_column_definedness_clauses();
	total_clauses += this->add_column_uniqueness_clauses();

	total_clauses += this->add_section_definedness_clauses();
	total_clauses += this->add_section_uniqueness_clauses();

	mClauses_temp_file.close();

	if (mVerbose) std::cout << "Done!" << std::endl;
	if (mVerbose) std::cout << "Generated a total of " << total_clauses << " clauses" << std::endl;
}

int Sudoku::add_single_cell_definedness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto y = 0; y < mSize; ++y) {
		for (auto x = 0; x < mSize; ++x) {
			++i;
			if (i % percent == 0)
				if (mVerbose) std::cout << "\rGenerating single-cell definedness clauses... \t\t" << int(double(i + 1) / size * 100) << "% ";

			std::vector<int> clause;

			for (auto n = 0; n < mSize; ++n) {
				if (mSudoku_matrix[x][y][n]) clause.push_back(get_luted_atom_number(x, y, n));
			}

			clause.push_back(0);
			write_clause(&clause);
			++generated_clauses;
		}
	}

	if (mVerbose) std::cout << "\rGenerating single-cell definedness clauses... \t\t100% ";

	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " single-cell definedness clauses." << std::endl;
	return generated_clauses;
}

int Sudoku::add_single_cell_uniqueness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto y = 0; y < mSize; ++y) {
		for (auto x = 0; x < mSize; ++x) {
			++i;
			if (i % percent == 0)
				if (mVerbose) std::cout << "\rGenerating single-cell uniqueness_clauses... \t\t" << int(double(i + 1) / size * 100) << "% ";

			std::vector<int> possible_numbers;
			for (auto n = 0; n < mSize; ++n) {
				if (!mSudoku_matrix[x][y][n]) continue;
				possible_numbers.push_back(get_luted_atom_number(x, y, n));
			}

			generated_clauses += encode_at_most_one(&possible_numbers);
		}
	}

	if (mVerbose) std::cout << "\rGenerating single-cell uniqueness_clauses... \t\t100% ";
	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " single-cell uniqueness clauses." << std::endl;
	return generated_clauses;
}

int Sudoku::add_row_uniqueness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto y = 0; y < mSize; ++y)
	{
		for (auto n = 0; n < mSize; ++n)
		{
			++i;
			if (i % percent == 0)
				if (mVerbose) std::cout << "\rGenerating row uniqueness clauses... \t\t\t" << int(double(i + 1) / size * 100) << "% ";

			std::vector<int> possible_numbers;
			for (auto x = 0; x < mSize; ++x)
			{
				if (!mSudoku_matrix[x][y][n]) continue;
				possible_numbers.push_back(get_luted_atom_number(x, y, n));	//collect all the positions of that number in the row
			}
			
			generated_clauses += encode_at_most_one(&possible_numbers);
		}
	}

	if (mVerbose) std::cout << "\rGenerating row uniqueness clauses... \t\t\t100% ";
	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " row uniqueness clauses." << std::endl;
	return generated_clauses;
}

int Sudoku::add_row_definedness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto y = 0; y < mSize; ++y)
	{
		for (auto n = 0; n < mSize; ++n)
		{
			++i;
			if (i % percent == 0)
				if (mVerbose) std::cout << "\rGenerating row definedness clauses... \t\t\t" << int(double(i + 1) / size * 100) << "% ";

			std::vector<int> clause;
			clause.reserve(mSize);
			for (auto x = 0; x < mSize; ++x)
			{
				if (mSudoku_matrix[x][y][n]) clause.push_back(get_luted_atom_number(x, y, n));
			}
			if (clause.size() > 1) {	//don't add unit clauses again
				clause.push_back(0);
				write_clause(&clause);
				++generated_clauses;
			}
		}
	}

	if (mVerbose) std::cout << "\rGenerating row definedness clauses... \t\t\t100% ";
	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " row definedness clauses." << std::endl;
	return generated_clauses;
}

int Sudoku::add_column_uniqueness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto x = 0; x < mSize; ++x)
	{
		for (auto n = 0; n < mSize; ++n)
		{
			++i;
			if (i % percent == 0)
				if (mVerbose) std::cout << "\rGenerating column uniqueness clauses... \t\t" << int(double(i + 1) / size * 100) << "% ";

			std::vector<int> possible_numbers;
			for (auto y = 0; y < mSize; ++y)
			{
				if (!mSudoku_matrix[x][y][n]) continue;
				possible_numbers.push_back(get_luted_atom_number(x, y, n));	//collect all the positions of that number in the column
			}

			generated_clauses += encode_at_most_one(&possible_numbers);
		}
	}

	if (mVerbose) std::cout << "\rGenerating column uniqueness clauses... \t\t100% ";
	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " column uniqueness clauses." << std::endl;
	return generated_clauses;
}

int Sudoku::add_column_definedness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto x = 0; x < mSize; ++x)
	{
		for (auto n = 0; n < mSize; ++n)
		{
			++i;
			if (i % percent == 0)
				if (mVerbose) std::cout << "\rGenerating column definedness clauses... \t\t" << int(double(i + 1) / size * 100) << "% ";

			std::vector<int> clause;
			clause.reserve(mSize);
			for (auto y = 0; y < mSize; ++y)
			{
				if (mSudoku_matrix[x][y][n]) clause.push_back(get_luted_atom_number(x, y, n));
			}
			if (clause.size() > 1) {	//don't add unit clauses again
				clause.push_back(0);
				write_clause(&clause);
				++generated_clauses;
			}
		}
	}

	if (mVerbose) std::cout << "\rGenerating column definedness clauses... \t\t100% ";
	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " column definedness clauses." << std::endl;
	return generated_clauses;
}

int Sudoku::add_section_uniqueness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto n = 0; n < mSize; ++n) {													//go through all the numbers
		for (auto section_x = 0; section_x < mN; ++section_x) {							//iterate through all the sections
			for (auto section_y = 0; section_y < mN; ++section_y) {
				++i;
				if (i % percent == 0)
					if (mVerbose) std::cout << "\rGenerating section uniqueness clauses... \t\t" << int(double(i + 1) / size * 100) << "% ";
				
				std::vector<int> possible_numbers;

				for (auto x_i = section_x * mN; x_i < (section_x + 1)*mN; ++x_i) {		//iterate through the single section
					for (auto y_i = section_y * mN; y_i < (section_y + 1)*mN; ++y_i)
					{
						if (!mSudoku_matrix[x_i][y_i][n]) continue;
						possible_numbers.push_back(get_luted_atom_number(x_i, y_i, n));
					}
				}

				generated_clauses += encode_at_most_one(&possible_numbers);
			}
		}
	}

	if (mVerbose) std::cout << "\rGenerating section uniqueness clauses... \t\t100% ";
	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " section uniqueness clauses." << std::endl;
	return generated_clauses;
}

int Sudoku::add_section_definedness_clauses()
{
	auto generated_clauses = 0;

	auto i = 0;
	const auto size = mSize * mSize;
	const auto percent = (size >= 100) ? size / 100 : 1;

	for (auto n = 0; n < mSize; ++n) {													//go through all the numbers
		for (auto section_x = 0; section_x < mN; ++section_x) {							//iterate through all the sections
			for (auto section_y = 0; section_y < mN; ++section_y) {
				++i;
				if (i % percent == 0)
					if (mVerbose) std::cout << "\rGenerating section definedness clauses... \t\t" << int(double(i + 1) / size * 100) << "% ";

				std::vector<int> clause;

				for (auto x_i = section_x * mN; x_i < (section_x + 1)*mN; ++x_i) {		//iterate through the single section
					for (auto y_i = section_y * mN; y_i < (section_y + 1)*mN; ++y_i)
					{
						if (mSudoku_matrix[x_i][y_i][n]) clause.push_back(get_luted_atom_number(x_i, y_i, n));
					}
				}
				if (clause.size() > 1) {	//don't add unit clauses again
					clause.push_back(0);
					write_clause(&clause);
					++generated_clauses;
				}
			}
		}
	}

	if (mVerbose) std::cout << "\rGenerating section definedness clauses... \t\t100% ";
	if (mVerbose) std::cout << "\tGenerated " << generated_clauses << " section definedness clauses." << std::endl;
	return generated_clauses;
}

void Sudoku::create_lut()
{
	if (mVerbose) std::cout << "Creating lookup table... ";

	//first entry is empty, because literals are 1-indexed
	mRead_lut.push_back(0);

	auto counter = 0;
	for (auto y = 0; y < mSize; ++y) {
		for (auto x = 0; x < mSize; ++x) {
			for (auto n = 0; n < mSize; ++n) {
				if (mSudoku_matrix[x][y][n])
				{
					const int atom_number = get_atom_number(x, y, n);

					//for every atom that's not definitely negative -> entry in the LUT
					mLut.at(atom_number) = ++counter;
					
					//now fill the read_lookuptable in reverse
					mRead_lut.push_back(atom_number);
				}
			}
		}
	}

	mNumber_of_atoms = counter;
	
	mExtra_atom_number = mNumber_of_atoms + 1;

	if (mVerbose) std::cout << "Done." << std::endl;
	if (mVerbose) std::cout << "Created " << counter << " entries in the lookup table." << std::endl;
}

//currently unneeded
void Sudoku::read_lut(const std::string& path)
{
	if (mVerbose) std::cout << "Reading lookup table... " << std::flush;

	std::ifstream file(path);
	std::string content;

	int i, j;

	auto counter = 0;

	std::stringstream ss;
	std::getline(file, content);
	ss << content;
	ss >> mSize;						//first number is size
	std::getline(file, content);
	ss.str("");
	ss.clear();
	ss << content;
	ss >> mNumber_of_atoms;		//second number is number of atoms

	mRead_lut.resize(mNumber_of_atoms+1, 0);

	while (!file.eof()) {
		ss.str("");
		ss.clear();
		std::getline(file, content);
		ss << content;
		ss >> i;
		ss >> j;
		
		mRead_lut.at(i) = j;
		++counter;
	}

	if (mVerbose) std::cout << "Done." << std::endl;
	if (mVerbose) std::cout << "Read " << counter << " entries in the lookup table." << std::endl;
}

//currently unneeded
void Sudoku::write_lut(const std::string& path) const
{
	std::ofstream file;

	file.open(path);

	//headline is size, next line is number of atoms
	file << mSize << "\n";
	file << mNumber_of_atoms << "\n";

	const auto cube = mSize * mSize * mSize;
	const auto square = mSize * mSize;

	for (auto i = 0; i < cube; ++i)
	{
		if (i % square == 0)
			if (mVerbose) std::cout << "\rWriting lookup table to file... \t\t\t" << int(double(i+1) / cube * 100) << "% ";
		if (mLut.at(i) != 0)
			file << mLut.at(i) << " " << i << "\n";
	}

	if (mVerbose) std::cout << "\rWriting lookup table to file... \t\t\t100% " << std::endl;
}

void Sudoku::write_clause(std::vector<int>* clause)
{
	for (auto lit : *clause)
	{
		mClauses_temp_file << lit << " ";
	}
	mClauses_temp_file << "\n";
	++mNumber_of_clauses;
}

void Sudoku::write_clauses()
{
	//close temp file in case it's still open
	if (mClauses_temp_file.is_open())
		mClauses_temp_file.close();
	std::ifstream in_file(mTemp_filename);
	
	//write header
	std::ofstream output_file(mClauses_output_filename);
	output_file << "p cnf " << mExtra_atom_number - 1 << " " << mNumber_of_clauses << "\n";

	//copy contents of temp file
	output_file << in_file.rdbuf();

	output_file.close();

	std::remove(mTemp_filename.c_str());
}

void Sudoku::read_solution(const std::string& path)
{
	if (mVerbose) std::cout << "Reading solution at \"" << path << "\"... ";

	std::ifstream file(path);
	std::string content;
	std::stringstream ss;

	char line_type;

	while (std::getline(file, content))
	{
		ss.str("");
		ss.clear();
		ss << content;
		ss >> line_type;
		if (line_type == 'v')
		{
			int lit;
			//the lines with the literals that we're looking for
			while (ss >> lit)
			{
				//only need to read positive and relevant literals
				if (lit > 0 && lit <= mNumber_of_atoms)
				{
					int x, y, n;
					this->get_position(lit, &x, &y, &n);
					this->set_field(x, y, n);
				}
			}
		}
	}

	if (mVerbose) std::cout << "Done!" << std::endl;
}

int Sudoku::get_size() const
{
	return mSize;
}

int Sudoku::get_n() const
{
	return mN;
}

bool Sudoku::is_solvable() const
{
	return mSolvable;
}

int Sudoku::get_ce_size() const
{
	return mCommander_encoding_size;
}

int Sudoku::get_number_of_clauses() const
{
	return mNumber_of_clauses;
}

void Sudoku::get_unsolvable_cell(int* x, int* y) const
{
	*x = mUnsolvable_cell_x;
	*y = mUnsolvable_cell_y;
}
