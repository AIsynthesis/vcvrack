CXX = g++
CXXFLAGS = -std=c++17 -I.. -Wall -Wextra -O2

all: test_ota_vcf test_looping_adsr test_bxr_engine test_matrix_mixer

test_ota_vcf: test_ota_vcf.cpp
	$(CXX) $(CXXFLAGS) -o test_ota_vcf test_ota_vcf.cpp -lm

test_looping_adsr: test_looping_adsr.cpp ../src/LoopingAdsr.hpp
	$(CXX) $(CXXFLAGS) -I../src -o test_looping_adsr test_looping_adsr.cpp

test_bxr_engine: test_bxr_engine.cpp ../src/BxrEngine.hpp
	$(CXX) $(CXXFLAGS) -I../src -o test_bxr_engine test_bxr_engine.cpp

test_matrix_mixer: test_matrix_mixer.cpp ../src/MatrixMixer.hpp
	$(CXX) $(CXXFLAGS) -I../src -o test_matrix_mixer test_matrix_mixer.cpp

.PHONY: all clean
clean:
	rm -f test_ota_vcf test_looping_adsr test_bxr_engine test_matrix_mixer
