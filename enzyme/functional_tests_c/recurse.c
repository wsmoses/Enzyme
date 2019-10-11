#include <stdio.h>
#include <math.h>
#include <assert.h>
#define __builtin_autodiff __enzyme_autodiff
extern "C" {
  double __enzyme_autodiff(...);
  int counter = 0;
  double recurse_max_helper(float* a, float* b, int N) {
    if (N <= 0) {
      return *a + *b;
    }
    return recurse_max_helper(a,b,N-1) + recurse_max_helper(a,b,N-2);
  }
  void recurse_max(float* a, float* b, float* ret, int N) {
    *ret = recurse_max_helper(a,b,N);
  }
}



int main(int argc, char** argv) {



  float a = 2.0;
  float b = 3.0;



  float da = 0;//(float*) malloc(sizeof(float));
  float db = 0;//(float*) malloc(sizeof(float));


  float ret = 0;
  float dret = 2.0;

  recurse_max(&a, &b, &ret, 20);

  int N = 20;
  int dN = 0;

  __builtin_autodiff(recurse_max, &a, &da, &b, &db, &ret, &dret, 20);


  assert(da == 17711.0*2);
  assert(db == 17711.0*2);



  printf("hello! %f, res2 %f, da: %f, db: %f\n", ret, ret, da,db);
  return 0;
}
