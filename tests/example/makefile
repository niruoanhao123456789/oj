CXX = g++
CXXFLAGS = -std=c++20 -g -Wall

mysql_test: mysql_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< -lpthread -lmysqlclient

file_test: file_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< -lpthread

.PHONY: test clean
test: mysql_test file_test
	@echo "=================== MySQL 模式 ==================="; ./mysql_test; s1=$$?; \
	echo "=================== file 模式 ====================="; ./file_test; s2=$$?; \
	if [ $$s1 -ne 0 ] || [ $$s2 -ne 0 ]; then exit 1; fi

clean:
	@ rm -f mysql_test file_test
	@ rm -rf filemodel
