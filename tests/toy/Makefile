CXX = g++
CXXFLAGS = --std=c++11 -O3 -g

debug: toy_clean toy_gprof

release: toy_clean toy_gprof

clean:
	@rm -f toy_clean toy_gprof gmon.out perf.data perf_output gprof_output

toy_clean: toy.cpp
	$(CXX) $(CXXFLAGS) -o toy_clean toy.cpp -pthread

toy_gprof: toy.cpp
	$(CXX) $(CXXFLAGS) -pg -o toy_gprof toy.cpp -pthread

gmon.out: toy_gprof
	./toy_gprof

gprof_output: gmon.out
	gprof --brief toy_gprof > gprof_output
	
perf.data: toy_clean
	perf record -e task-clock -c 1000000 ./toy_clean

perf_output: perf.data
	perf report > perf_output

test: gprof_output perf_output
