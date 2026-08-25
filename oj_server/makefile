HPP = oj_control.hpp oj_filemodel.hpp oj_view.hpp ../common/log/*.hpp ../common/Util.hpp

oj_server:oj_server.cpp $(HPP)
	g++ -o $@ $< -std=c++20 -ljsoncpp -lpthread -lcpp-httplib -lctemplate

.PHONY:
clean:
	@ rm -f oj_server