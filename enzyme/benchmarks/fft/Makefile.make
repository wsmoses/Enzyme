# RUN: cd %desired_wd/fft && LD_LIBRARY_PATH="%bldpath:$LD_LIBRARY_PATH" BENCH="%bench" BENCHLINK="%blink" LOAD="%loadEnzyme" make -B fft-unopt.ll fft-raw.ll fft-opt.ll results.txt VERBOSE=1 -f %s

.PHONY: clean

clean:
	rm -f *.ll *.o results.txt

%-unopt.ll: %.cpp
	#clang++ $(BENCH) $^ -O2 -fno-use-cxa-atexit -fno-vectorize -fno-slp-vectorize -ffast-math -fno-unroll-loops -o $@ -S -emit-llvm
	clang++ $(BENCH) $^ -O1 -disable-llvm-optzns -fno-use-cxa-atexit -fno-vectorize -fno-slp-vectorize -ffast-math -fno-unroll-loops -o $@ -S -emit-llvm

%-raw.ll: %-unopt.ll
	opt $^ $(LOAD) -enzyme -o $@ -S

%-opt.ll: %-raw.ll
	opt-8 $^ -O2 -o $@ -S

fft.o: fft-opt.ll
	#clang++ $^ -o $@ -lblas $(BENCHLINK)
	clang++ -O2 $^ -o $@ -lblas $(BENCHLINK)

results.txt: fft.o
	./$^ 1048576 | tee $@
