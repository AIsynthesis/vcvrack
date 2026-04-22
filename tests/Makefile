CXX = g++
CXXFLAGS = -std=c++17 -I.. -Wall -Wextra -O2

test_ota_vcf: test_ota_vcf.cpp
	$(CXX) $(CXXFLAGS) -o test_ota_vcf test_ota_vcf.cpp -lm

.PHONY: clean
clean:
	rm -f test_ota_vcf
