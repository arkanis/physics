#include <stdlib.h>
#include <stdio.h>

#include "model.h"

model_p model_new(){
	model_p m = malloc(sizeof(model_t));
	*m = (model_t){
		.particle_count = 0,
		.particles = NULL,
		.beam_count = 0,
		.beams = NULL,
		.thruster_count = 0,
		.thrusters = NULL
	};
	return m;
}

void model_destroy(model_p model){
	free(model);
}


void model_add_particle(model_p model, float x, float y, float mass){
	model->particle_count++;
	model->particles = realloc(model->particles, sizeof(particle_t) * model->particle_count);
	
	model->particles[model->particle_count-1] = (particle_t){
		.pos = (vec2_t){ x, y },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = mass
	};
}

void model_add_beam(model_p model, size_t from_idx, size_t to_idx){
	model->beam_count++;
	model->beams = realloc(model->beams, sizeof(beam_t) * model->beam_count);
	
	model->beams[model->beam_count-1] = (beam_t){
		.i1 = from_idx, .i2 = to_idx,
		.length = v2_length( v2_sub(model->particles[to_idx].pos, model->particles[from_idx].pos) )
	};
}

void model_add_thruster(model_p model, size_t from_idx, size_t to_idx, float force, uint8_t controlled_by){
	model->thruster_count++;
	model->thrusters = realloc(model->thrusters, sizeof(thruster_t) * model->thruster_count);
	
	model->thrusters[model->thruster_count-1] = (thruster_t){
		.i1 = from_idx, .i2 = to_idx,
		.force = force,
		.controlled_by = controlled_by
	};
}


void model_save(model_p model, const char *filename){
	FILE *file = fopen(filename, "w");
	if (file == NULL){
		perror("model_save: fopen");
		return;
	}
	
	for(size_t i = 0; i < model->particle_count; i++){
		particle_p p = &model->particles[i];
		fprintf(file, "p %f %f %f\n", p->pos.x, p->pos.y, p->mass);
	}
	
	for(size_t i = 0; i < model->beam_count; i++){
		beam_p beam = &model->beams[i];
		fprintf(file, "b %zu %zu\n", beam->i1, beam->i2);
	}
	
	for(size_t i = 0; i < model->thruster_count; i++){
		thruster_p t = &model->thrusters[i];
		fprintf(file, "t %zu %zu %f %x\n", t->i1, t->i2, t->force, t->controlled_by);
	}
	
	fclose(file);
	printf("saved model %p to %s\n", model, filename);
}

void model_load(model_p model, const char *filename){
	const size_t line_limit = 512;
	char line[line_limit];
	
	FILE* file = fopen(filename, "r");
	if (file == NULL){
		perror("model_load: fopen");
		return;
	}
	
	// First count the number of each element type
	model->particle_count = 0;
	model->beam_count = 0;
	model->thruster_count = 0;
	
	while( fgets(line, line_limit, file) != NULL ){
		switch(line[0]){
			case 'p':  // particle
				model->particle_count++;
				break;
			case 'b': // beam
				model->beam_count++;
				break;
			case 't': // thruster
				model->thruster_count++;
				break;
		}
	}
	printf("model %s: %zu particles, %zu beams, %zu thrusters\n", filename,
		model->particle_count, model->beam_count, model->thruster_count);
	
	// Now we know how many particles and beams we need, allocate them
	model->particles = realloc(model->particles, sizeof(particle_t) * model->particle_count);
	model->beams = realloc(model->beams, sizeof(beam_t) * model->beam_count);
	model->thrusters = realloc(model->thrusters, sizeof(thruster_t) * model->thruster_count);
	
	// Load the model again but this time we're not counting but extracting all values to build
	// all elements of the model.
	rewind(file);
	
	size_t particle_idx = 0, beam_idx = 0, thruster_idx = 0;
	float x, y, mass, force;
	size_t i1, i2;
	int controlled_by;
	
	while( fgets(line, line_limit, file) != NULL ){
		switch(line[0]){
			case 'p':  // particle
				if (particle_idx >= model->particle_count)
					break;
				sscanf(line, "p %f %f %f", &x, &y, &mass);
				printf("particles[%zu] at %f %f mass %f\n", particle_idx, x, y, mass);
				model->particles[particle_idx] = (particle_t){
					.pos = (vec2_t){ x, y },
					.vel = (vec2_t){ 0, 0 },
					.force = (vec2_t){0, 0},
					.mass = mass
				};
				particle_idx++;
				break;
			case 'b': // beam
				if (beam_idx >= model->beam_count)
					break;
				sscanf(line, "b %zu %zu", &i1, &i2);
				printf("beams[%zu] from %zu to %zu\n", beam_idx, i1, i2);
				model->beams[beam_idx] = (beam_t){
					.i1 = i1, .i2 = i2,
					.length = v2_length( v2_sub(model->particles[i2].pos, model->particles[i1].pos) )
				};
				beam_idx++;
				break;
			case 't': // thruster
				if (thruster_idx >= model->thruster_count)
					break;
				sscanf(line, "t %zu %zu %f %x", &i1, &i2, &force, &controlled_by);
				printf("thrusters[%zu] from %zu to %zu with force %f, controll mask 0x%02x\n",
					thruster_idx, i1, i2, force, controlled_by);
				model->thrusters[thruster_idx] = (thruster_t){
					.i1 = i1, .i2 = i2,
					.force = force,
					.controlled_by = controlled_by
				};
				thruster_idx++;
		}
	}
	
	fclose(file);
	
	printf("loaded model %p from %s\n", model, filename);
}


vec2_t model_particle_center(model_p model){
	vec2_t center = {0, 0};
	for(size_t i = 0; i < model->particle_count; i++){
		center.x += model->particles[i].pos.x / model->particle_count;
		center.y += model->particles[i].pos.y / model->particle_count;
	}
	return center;
}