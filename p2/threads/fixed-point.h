#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

/* Definition of fixed point type */
typedef int fixed_point_t;
/* 17 bits before the decimal point */
#define p 17
/* 14 bits after the decimal point */
#define q 14
/* f = 2**q */
#define f 1 << q

/* Convert a real number to fixed-point number */
#define FP_CONVERT_TO_FP(N) ((fixed_point_t)((N) * (f)))
/* Convert a fixed-point number to integer number, towards ZERO */
#define FP_CONVERT_TO_INT_ZERO(X) ((int)((X) / (f)))
/* Convert a fixed-point number to integer number, towards NEAREST */
#define FP_CONVERT_TO_INT_NEAREST(X) (((X)>=0) ? ((int)(((X)+(f)/2)/(f))) : ((int)(((X)-(f)/2)/(f))))

/* Fixed-point adding */
#define FP_ADD(X, Y) ((X) + (Y))
/* Fixed-point subtracting */
#define FP_SUB(X, Y) ((X) - (Y))
/* Add fixed-point X and int N */
#define FP_FP_ADD_INT(X, N) ((X) + (N)*(f))
/* Substract int N from fixed-point X */
#define FP_FP_SUB_INT(X, N) ((X) - (N)*(f))

/* Multiply two fixed-point numbers */
#define FP_MULT(X, Y) (((int64_t)(X)) * (Y) / (f))
/* Multiply fixed-point X by int N */
#define FP_FP_MULT_INT(X, N) ((X) * (N))
/* Divide two fixed-point numbers */
#define FP_DIV(X, Y) (((int64_t)(X)) * (f) / (Y))
/* Divide fixed-point X by int N */
#define FP_FP_DIV_INT(X, N) ((X) / (N))

#endif
