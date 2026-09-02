.PHONY: all
all:
	@ cd compile_server;\
	make;\
	cd -;\
	cd oj_server;\
	make;\
	cd -;

.PHONY: test
test:
	@ cd tests/unit;\
	make clean;\
	make;\
	./unit_tests;\
	cd -;

.PHONY: output
output:
	@ mkdir -p output/compile_server;\
	mkdir -p output/oj_server;\
	cp -rf compile_server/compile_server output/compile_server/;\
	cp -rf compile_server/temp output/compile_server/;\
	cp -rf oj_server/oj_server output/oj_server/;\
	cp -rf oj_server/conf output/oj_server/;\
	cp -rf oj_server/database output/oj_server/;\
	cp -rf oj_server/questions output/oj_server/;\
	cp -rf oj_server/template_html output/oj_server/;\
	cp -rf oj_server/wwwroot output/oj_server/;

.PHONY: clean
clean:
	@ cd compile_server;\
	make clean;\
	cd -;\
	cd oj_server;\
	make clean;\
	cd -;\
	cd tests/unit;\
	make clean;\
	cd -;\
	cd tests/example;\
	make clean;\
	cd -;\
	rm -rf output;