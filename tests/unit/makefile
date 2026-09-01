CXX = g++
CXXFLAGS = -std=c++20 -g -Wall
GTEST_LIBS = -lgtest -lgtest_main -lpthread -lmysqlclient

TESTS = test_passwd test_question test_filemodel test_mysqlmodel test_session

unit_tests: $(addsuffix .o,$(TESTS))
	$(CXX) -o $@ $^ $(GTEST_LIBS)

%.o: %.cpp test_env.hpp
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

.PHONY: test clean
test: unit_tests
	./unit_tests

clean:
	@ rm -f unit_tests *.o
