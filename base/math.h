#pragma once

typedef float mat4_t[16];
typedef float mat3_t[9];
typedef struct { float x, y; } vec2_t;
typedef struct { float x, y, z; } vec3_t;

void m3_transpose(mat3_t mr, mat3_t ma);
void m3_identity(mat3_t mat);
float m3_det(mat3_t mat);
void m3_inverse(mat3_t mr, mat3_t ma);
void m3_m3_mul(mat3_t mr, mat3_t m1, mat3_t m2);
vec3_t m3_v3_mul(mat3_t mat, vec3_t v);
vec2_t m3_v2_mul(mat3_t mat, vec2_t v);