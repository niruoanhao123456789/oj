HPP = compile_run.hpp compiler.hpp runner.hpp ../log/*.hpp ../common/Util.hpp
TempFile = ./temp/*.exe ./temp/*stdin ./temp/*.stdout ./temp/*.stderr ./temp/*.compile_error

compile_server:compile_server.cpp $(HPP)
	g++ -o $@ $< -std=c++20 -ljsoncpp

.PHONY:
clean:
	@ rm -f compile_server
	@ rm -f $(TempFile)