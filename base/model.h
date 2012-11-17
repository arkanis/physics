#pragma once

#include <stddef.h>
#include <stdint.h>
#include "math.h"

/**

Notes:
- Beams and thrusters use indices of the particles instead pointers. Otherwise we would have to
  adjust each pointer when the particles array is moved by realloc() and the memory addresses
  change.

*/

typedef struct {
	vec2_t pos, vel, force;  // m, m_s, m_s2
	float mass;  // kg
	int flags;
} particle_t, *particle_p;

#define PARTICLE_TRAVERSED	1<<0
#define PARTICLE_SELECTED	1<<1


typedef struct {
	size_t i1, i2;  // indices of the connected particles
	float length;  // m
	int flags;
} beam_t, *beam_p;

#define BEAM_TRAVERSED	1<<0
#define BEAM_FOLLOWED	1<<1
#define BEAM_BROKEN		1<<2


typedef struct {
	size_t i1, i2;  // indices of the connected particles
	float force;  // N
	uint8_t controlled_by;
} thruster_t, *thruster_p;

#define THRUSTER_BACK		1<<0
#define THRUSTER_FRONT	1<<1
#define THRUSTER_LEFT		1<<2
#define THRUSTER_RIGHT	1<<3


typedef struct {
	size_t particle_count, beam_count, thruster_count;
	particle_p particles;
	beam_p beams;
	thruster_p thrusters;
} model_t, *model_p;


model_p model_new();
void model_destroy(model_p model);

void model_add_particle(model_p model, float x, float y, float mass);
void model_add_beam(model_p model, size_t from_idx, size_t to_idx);
void model_add_thruster(model_p model, size_t from_idx, size_t to_idx, float force, uint8_t controlled_by);

void model_save(model_p model, const char *filename);
void model_load(model_p model, const char *filename);

vec2_t model_particle_center(model_p model);