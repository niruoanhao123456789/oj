HPP = oj_control.hpp oj_model.hpp oj_view.hpp ../common/log/*.hpp ../common/Util.hpp

oj_server:oj_server.cpp $(HPP)
	g++ -o $@ $< -std=c++20 -ljsoncpp -lpthread -lcpp-httplib

.PHONY:
clean:
	@ rm -f oj_server