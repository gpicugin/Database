#test

LIBS = sqlite3

all:
	g++ -std=c++11 main.cpp DejitterUtils.cpp QOSDatabase.cpp Timer.cpp -o test.exe

	