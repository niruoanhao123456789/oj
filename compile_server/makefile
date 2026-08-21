HPP = compile_run.hpp compiler.hpp runner.hpp ../common/log/*.hpp ../common/Util.hpp
TempFile = ./temp/*

compile_server:compile_server.cpp $(HPP)
	g++ -o $@ $< -std=c++20 -ljsoncpp -lpthread -lcpp-httplib

.PHONY:
clean:
	@ rm -f compile_server
	@ rm -f $(TempFile)