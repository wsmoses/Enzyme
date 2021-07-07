// RUN: %clang -std=c11 -O0 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme -S | %lli - 
// RUN: %clang -std=c11 -O1 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme -S | %lli - 
// RUN: %clang -std=c11 -O2 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme -S | %lli - 
// RUN: %clang -std=c11 -O3 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme -S | %lli - 
// RUN: %clang -std=c11 -O0 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme --enzyme-inline=1 -S | %lli - 
// RUN: %clang -std=c11 -O1 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme --enzyme-inline=1 -S | %lli - 
// RUN: %clang -std=c11 -O2 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme --enzyme-inline=1 -S | %lli - 
// RUN: %clang -std=c11 -O3 %s -S -emit-llvm -o - | %opt - %loadEnzyme -enzyme --enzyme-inline=1 -S | %lli - 

#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "test_utils.h"

#include <mpi.h>

// XFAIL: *

double __enzyme_autodiff(void*, ...);

double mpi_reduce_test(double b, int n, int rank, int numprocs) {
    MPI_Reduce(&b, &global_sum, 1, MPI_DOUBLE, MPI_SUM, 0,
           MPI_COMM_WORLD);
           
    return global_sum;
}

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  double h=1e-6;
  if(argc<2) {
    printf("Not enough arguments. Missing problem size.");
    MPI_Finalize();
    return 0;
  }
  int numprocs;
  MPI_Comm_size(MPI_COMM_WORLD,&numprocs);
  int N=10;
  
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  double res = __enzyme_autodiff((void*)mpi_bcast_test, 10.0+rank, N, rank, numprocs);
  printf("res=%f rank=%d\n", res, rank);
  fflush(0);
  MPI_Finalize();
}

