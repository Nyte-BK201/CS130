#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

/* P.Q for fixed-point; F is multiplier */
#define P 17
#define Q 14
#define F 1 << (Q)

typedef int fixed_point;

/* n is int ; x y is fixed-point */
#define INT_TO_FP(n) (n) * (F)
#define FP_TO_INT_TRUNC(x) (x) / (F)
#define FP_TO_INT_ROUND(x) ((x) >= 0 ? ((x) + (F)/2)/(F) : ((x) - (F)/2)/(F))

#define FP_ADD_FP(x, y) (x) + (y) 
#define FP_SUB_FP(x, y) (x) - (y)

#define FP_ADD_INT(x, n) (x) + (n) * (F)
#define FP_SUB_INT(x, n) (x) - (n) * (F)

#define FP_MUL_FP(x, y) ((int64_t)(x)) * (y) / (F)
#define FP_MUL_INT(x, n) (x) * (n)

#define FP_DIV_FP(x, y) ((int64_t)(x)) * (F) / (y)
#define FP_DIV_INT(x, n) (x) / (n)


#endif