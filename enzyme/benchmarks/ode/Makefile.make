# RUN: cd %desired_wd/ode && LD_LIBRARY_PATH="%bldpath:$LD_LIBRARY_PATH" BENCH="%bench" BENCHLINK="%blink" LOAD="%loadEnzyme" make -B ode-unopt.ll ode-opt.ll ode.o results.txt VERBOSE=1 -f %s

.PHONY: clean

clean:
	rm -f *.ll *.o results.txt

ode-adept-unopt.ll: ode-adept.cpp
	#clang++ $(BENCH) $^ -O2 -fno-use-cxa-atexit -fno-vectorize -fno-slp-vectorize -ffast-math -fno-unroll-loops -o $@ -S -emit-llvm
	clang++ $(BENCH) $^ -O1 -Xclang -disable-llvm-passes -fno-use-cxa-atexit -fno-vectorize -fno-slp-vectorize -ffast-math -fno-unroll-loops -o $@ -S -emit-llvm

ode-unopt.ll: ode.cpp
	#clang++ $(BENCH) $^ -O2 -fno-use-cxa-atexit -fno-vectorize -fno-slp-vectorize -ffast-math -fno-unroll-loops -o $@ -S -emit-llvm
	#clang++ $(BENCH) $^ -O1 -fno-use-cxa-atexit -fno-exceptions -fno-vectorize -fno-slp-vectorize -ffast-math -fno-unroll-loops -o $@ -S -emit-llvm -Xclang -new-struct-path-tbaa
	clang++ $(BENCH) $^ -O1 -Xclang -disable-llvm-passes -fno-use-cxa-atexit -fno-exceptions -fno-vectorize -fno-slp-vectorize -ffast-math -fno-unroll-loops -o $@ -S -emit-llvm -Xclang -new-struct-path-tbaa

ode-raw.ll: ode-adept-unopt.ll ode-unopt.ll
	opt ode-unopt.ll $(LOAD) -enzyme -o ode-enzyme.ll -S -enzyme_loosetypes
	#opt -inline ode-unopt.ll $(LOAD) -enzyme -o ode-enzyme.ll -S -enzyme_loosetypes
	llvm-link ode-adept-unopt.ll ode-enzyme.ll -o $@

%-opt.ll: %-raw.ll
	opt-8 $^ -o $@ -S
	#opt-8 $^ -O2 -o $@ -S

ode.o: ode-opt.ll
	#clang++ $^ -o $@ -lblas $(BENCHLINK)
	clang++ -O2 $^ -o $@ -lblas $(BENCHLINK)

results.txt: ode.o
	./$^ 1000000 | tee $@
