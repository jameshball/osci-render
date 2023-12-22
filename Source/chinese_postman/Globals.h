#pragma once

#define EPSILON 0.000001
#define INFINITO 1000000000.0
#define GREATER(A, B) ((A) - (B) > EPSILON)
#define LESS(A, B) ((B) - (A) > EPSILON)
#define EQUAL(A, B) (fabs((A) - (B)) < EPSILON)
#define GREATER_EQUAL(A, B) (GREATER((A),(B)) || EQUAL((A),(B)))
#define LESS_EQUAL(A, B) (LESS((A),(B)) || EQUAL((A),(B)))
#define MIN(A, B) (LESS((A),(B)) ? (A) : (B))
#define MAX(A, B) (LESS((A),(B)) ? (B) : (A))

