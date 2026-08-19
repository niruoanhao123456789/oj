HPP = complie_run.hpp complier.hpp runner.hpp ../log/*.hpp ../common/Util.hpp

complie_server:complie_server.cpp $(HPP)
	g++ -o $@ $< -std=c++20

.PHONY:
clean:
	rm -f complie_server