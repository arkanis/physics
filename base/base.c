#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#define __USE_XOPEN 1
#include <math.h>

#include <SDL/SDL.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include "common.h"
#include "math.h"
#include "viewport.h"



/*

Resource actions:
- load
- new
- destroy
- unload

Units:
- World space: meter [m]

Pipeline:
- transform * local coords -> world coords
- camera * world coords -> normalized device coords
- viewport * normalized device coords -> screen coords

*/

// Viewport of the renderer. Data from the viewport is used by other components.
viewport_p viewport;
bool debug = false;

//
// Grid
//
GLuint grid_prog, grid_vertex_buffer;
// Space between grid lines in world units
vec2_t grid_default_spacing = {1, 1};

void grid_load(){
	grid_prog = load_and_link_program("grid.vs", "grid.ps");
	assert(grid_prog != 0);
	
	glGenBuffers(1, &grid_vertex_buffer);
	assert(grid_vertex_buffer != 0);
	glBindBuffer(GL_ARRAY_BUFFER, grid_vertex_buffer);
	
	// Rectangle
	const float vertecies[] = {
		1, 1,
		-1, 1,
		-1, -1,
		1, -1
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertecies), vertecies, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void grid_unload(){
	glDeleteBuffers(1, &grid_vertex_buffer);
	delete_program_and_shaders(grid_prog);
}

void grid_draw(){
	vec2_t grid_spacing = (vec2_t){
		grid_default_spacing.x * viewport->world_to_screen[0],
		grid_default_spacing.y * viewport->world_to_screen[4]
	};
	vec2_t grid_offset = (vec2_t){
		viewport->pos.x * viewport->world_to_screen[0],
		viewport->pos.y * viewport->world_to_screen[4]
	};
	
	glUseProgram(grid_prog);
	glBindBuffer(GL_ARRAY_BUFFER, grid_vertex_buffer);
	
	GLint pos_attrib = glGetAttribLocation(grid_prog, "pos");
	glEnableVertexAttribArray(pos_attrib);
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
	
	glUniform4f( glGetUniformLocation(grid_prog, "color"), 0, 0, 0.5, 1 );
	glUniform2f( glGetUniformLocation(grid_prog, "grid_spacing"), grid_spacing.x, grid_spacing.y );
	glUniform2f( glGetUniformLocation(grid_prog, "grid_offset"), grid_offset.x, grid_offset.y );
	glUniform1f( glGetUniformLocation(grid_prog, "vp_scale_exp"), viewport->scale_exp );
	glUniform2f( glGetUniformLocation(grid_prog, "screen_size"), viewport->screen_size.x, viewport->screen_size.y );
	glDrawArrays(GL_QUADS, 0, 4);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}


//
// Cursor
//
vec2_t cursor_pos = {0, 0};
color_t cursor_color = {1, 1, 1, 1};
GLuint cursor_prog, cursor_vertex_buffer;

void cursor_load(){
	cursor_prog = load_and_link_program("cursor.vs", "cursor.ps");
	assert(cursor_prog != 0);
	
	glGenBuffers(1, &cursor_vertex_buffer);
	assert(cursor_vertex_buffer != 0);
	glBindBuffer(GL_ARRAY_BUFFER, cursor_vertex_buffer);
	
	// Rectangle
	const float vertecies[] = {
		5, 5,
		-5, 5,
		-5, -5,
		5, -5
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertecies), vertecies, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void cursor_unload(){
	glDeleteBuffers(1, &cursor_vertex_buffer);
	delete_program_and_shaders(cursor_prog);
}

void cursor_draw(){
	//printf("cursor pos: x %f y %f\n", cursor_pos.x, cursor_pos.y);
	
	glUseProgram(cursor_prog);
	glBindBuffer(GL_ARRAY_BUFFER, cursor_vertex_buffer);
	
	GLint pos_attrib = glGetAttribLocation(cursor_prog, "pos");
	glEnableVertexAttribArray(pos_attrib);
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
	
	GLint cursor_pos_uni = glGetUniformLocation(cursor_prog, "cursor_pos");
	assert(cursor_pos_uni != -1);
	glUniform2f(cursor_pos_uni, cursor_pos.x, cursor_pos.y);
	
	GLint color_uni = glGetUniformLocation(cursor_prog, "color");
	assert(color_uni != -1);
	glUniform4f(color_uni, cursor_color.r, cursor_color.g, cursor_color.b, cursor_color.a);
	
	GLint projection_uni = glGetUniformLocation(cursor_prog, "projection");
	assert(projection_uni != -1);
	glUniformMatrix3fv(projection_uni , 1, GL_FALSE, viewport->screen_to_normal);
	
	glDrawArrays(GL_QUADS, 0, 4);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}


//
// Particles
//
typedef struct {
	vec2_t pos, vel, force;  // m, m_s, m_s2
	float mass;  // kg
	int flags;
} particle_t, *particle_p;

#define PARTICLE_TRAVERSED 1<<0
#define PARTICLE_SELECTED 1<<1

particle_p particles = NULL;
size_t particle_count = 0;
GLuint particle_prog, particle_vertex_buffer;

typedef struct {
	particle_p p1, p2;
	float length;  // m
	int flags;
} beam_t, *beam_p;

#define BEAM_TRAVERSED 1<<0
#define BEAM_FOLLOWED 1<<1
#define BEAM_BROKEN 1<<2

beam_p beams = NULL;
size_t beam_count = 0;
GLuint beam_prog, beam_vertex_buffer;

// Thruster forward declaration to create thrusters when loading a model
typedef struct {
	size_t p1_idx, p2_idx;
	float force;
	uint8_t controlled_by;
} thruster_t, *thruster_p;

#define THRUSTER_BACK 1<<0
#define THRUSTER_FRONT 1<<1
#define THRUSTER_LEFT 1<<2
#define THRUSTER_RIGHT 1<<3

thruster_p thrusters = NULL;
size_t thruster_count = 0;

void particles_save_model(const char *filename){
	FILE *file = fopen(filename, "w");
	
	for(size_t i = 0; i < particle_count; i++){
		particle_p p = &particles[i];
		fprintf(file, "p %f %f %f\n", p->pos.x, p->pos.y, p->mass);
	}
	
	for(size_t i = 0; i < beam_count; i++){
		beam_p beam = &beams[i];
		
		if (beam == NULL)
			continue;
		
		fprintf(file, "b %zu %zu\n", (beam->p1 - particles), (beam->p2 - particles));
	}
	
	for(size_t i = 0; i < thruster_count; i++){
		thruster_p t = &thrusters[i];
		fprintf(file, "t %zu %zu %f %2x\n", t->p1_idx, t->p2_idx, t->force, t->controlled_by);
	}
	
	fclose(file);
	printf("saved current mesh to %s\n", filename);
}

void particles_load_model(const char *filename){
	const size_t line_limit = 512;
	char line[line_limit];
	
	FILE* file = fopen(filename, "r");
	
	// First count the number of particles and beams
	particle_count = 0;
	beam_count = 0;
	thruster_count = 0;
	while( fgets(line, line_limit, file) != NULL ){
		switch(line[0]){
			case 'p':  // particle
				particle_count++;
				break;
			case 'b': // beam
				beam_count++;
				break;
			case 't': // thruster
				thruster_count++;
				break;
		}
	}
	printf("model %s: %zu particles, %zu beams\n", filename, particle_count, beam_count);
	
	// Now we know how many particles and beams we need, allocate them
	particles = realloc(particles, sizeof(particle_t) * particle_count);
	beams = realloc(beams, sizeof(beam_t) * beam_count);
	thrusters = realloc(thrusters, sizeof(thruster_t) * thruster_count);
	
	// Load the model again but this time extract the values into our particles and beams
	rewind(file);
	
	size_t particle_idx = 0, beam_idx = 0, thruster_idx = 0;
	float x, y, mass, force;
	size_t from_idx, to_idx;
	int controlled_by;
	while( fgets(line, line_limit, file) != NULL ){
		switch(line[0]){
			case 'p':  // particle
				if (particle_idx >= particle_count)
					break;
				sscanf(line, "p %f %f %f", &x, &y, &mass);
				printf("particles[%zu] at %f %f mass %f\n", particle_idx, x, y, mass);
				particles[particle_idx] = (particle_t){
					.pos = (vec2_t){ x, y },
					.vel = (vec2_t){ 0, 0 },
					.force = (vec2_t){0, 0},
					.mass = mass
				};
				particle_idx++;
				break;
			case 'b': // beam
				if (beam_idx >= beam_count)
					break;
				sscanf(line, "b %zu %zu", &from_idx, &to_idx);
				printf("beams[%zu] from %zu to %zu\n", beam_idx, from_idx, to_idx);
				beams[beam_idx] = (beam_t){
					.p1 = &particles[from_idx],
					.p2 = &particles[to_idx],
					.length = v2_length( v2_sub(particles[to_idx].pos, particles[from_idx].pos) )
				};
				beam_idx++;
				break;
			case 't': // thruster
				if (thruster_idx >= thruster_count)
					break;
				sscanf(line, "t %zu %zu %f %x", &from_idx, &to_idx, &force, &controlled_by);
				thrusters[thruster_idx] = (thruster_t){
					.p1_idx = from_idx, .p2_idx = to_idx,
					.force = force,
					.controlled_by = controlled_by
				};
				thruster_idx++;
		}
	}
	
	fclose(file);
	
	printf("loaded mesh from %s\n", filename);
}

void particles_add(float x, float y, float mass){
	particle_p old_particle_addr = particles;
	
	particle_count++;
	particles = realloc(particles, sizeof(particle_t) * particle_count);
	
	particles[particle_count-1] = (particle_t){
		.pos = (vec2_t){ x, y },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = mass
	};
	
	// Update the pointers in the beams array by calculating the old index and then a new pointer
	for(size_t i = 0; i < beam_count; i++){
		beam_p beam = &beams[i];
		
		if (beam == NULL)
			continue;
		
		beam->p1 = &particles[beam->p1 - old_particle_addr];
		beam->p2 = &particles[beam->p2 - old_particle_addr];
	}
}

void particles_add_beam(particle_p from, particle_p to){
	beam_count++;
	beams = realloc(beams, sizeof(beam_t) * beam_count);
	
	beams[beam_count-1] = (beam_t){
		.p1 = from,
		.p2 = to,
		.length = v2_length( v2_sub(to->pos, from->pos) )
	};
}


void particles_load(){
	beam_prog = load_and_link_program("unit.vs", "unit.ps");
	assert(beam_prog != 0);
	
	glGenBuffers(1, &beam_vertex_buffer);
	assert(beam_vertex_buffer != 0);
	
	particle_prog = load_and_link_program("particle.vs", "particle.ps");
	assert(particle_prog != 0);
	
	glGenBuffers(1, &particle_vertex_buffer);
	assert(particle_vertex_buffer != 0);
	glBindBuffer(GL_ARRAY_BUFFER, particle_vertex_buffer);
	
	const float vertecies[] = {
		// Rectangle
		0.5, 0.5,
		-0.5, 0.5,
		-0.5, -0.5,
		0.5, -0.5,
		// Arrow: -->
		1, 0,
		0.25, 0.25,
		0, -1,
		0.25, -0.25
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertecies), vertecies, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void particles_unload(){
	free(particles);
	particles = NULL;
	
	free(beams);
	beams = NULL;
	
	glDeleteBuffers(1, &particle_vertex_buffer);
	delete_program_and_shaders(particle_prog);
	
	glDeleteBuffers(1, &beam_vertex_buffer);
	delete_program_and_shaders(beam_prog);
}

void particles_draw(){
	// Draw particles
	glUseProgram(particle_prog);
	glBindBuffer(GL_ARRAY_BUFFER, particle_vertex_buffer);
	
	GLint pos_attrib = glGetAttribLocation(particle_prog, "pos");
	assert(pos_attrib != -1);
	glEnableVertexAttribArray(pos_attrib);
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
	
	GLint color_uni = glGetUniformLocation(particle_prog, "color");
	GLint to_norm_uni = glGetUniformLocation(particle_prog, "to_norm");
	GLint trans_uni = glGetUniformLocation(particle_prog, "transform");
	assert(color_uni != -1);
	assert(to_norm_uni != -1);
	assert(trans_uni != -1);
	
	glUniformMatrix3fv(to_norm_uni, 1, GL_FALSE, viewport->world_to_normal);
	
	for(size_t i = 0; i < particle_count; i++){
		glUniformMatrix3fv(trans_uni, 1, GL_TRUE, (float[9]){
			0.25, 0, particles[i].pos.x,
			0, 0.25, particles[i].pos.y,
			0, 0, 1
		});
		
		if (particles[i].flags & PARTICLE_SELECTED)
			glUniform4f(color_uni, 1, 0, 0, 1 );
		else
			glUniform4f(color_uni, 0, 1, 0, 1 );
		
		glDrawArrays(GL_QUADS, 0, 4);
	}
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	
	
	// Draw beams
	glUseProgram(beam_prog);
	glBindBuffer(GL_ARRAY_BUFFER, beam_vertex_buffer);
	
	size_t unbroken_beam_count = 0;
	for(size_t i = 0; i < beam_count; i++){
		if ( !(beams[i].flags & BEAM_BROKEN) )
			unbroken_beam_count++;
	}
	
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * unbroken_beam_count, NULL, GL_STATIC_DRAW);
	float *vertex_buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
	size_t vi = 0;
	for(size_t i = 0; i < beam_count; i++){
		if (beams[i].flags & BEAM_BROKEN)
			continue;
		vertex_buffer[vi*4+0] = beams[i].p1->pos.x;
		vertex_buffer[vi*4+1] = beams[i].p1->pos.y;
		vertex_buffer[vi*4+2] = beams[i].p2->pos.x;
		vertex_buffer[vi*4+3] = beams[i].p2->pos.y;
		vi++;
		//printf("beam %zu: from %f/%f to %f/%f\n", i, beams[i].p1->pos.x, beams[i].p1->pos.y, beams[i].p2->pos.x, beams[i].p2->pos.y);
	}
	/*
	for(size_t i = 0; i < 4 * beam_count; i++)
		printf("vb[%zu]: %f\n", i, vertex_buffer[i]);
	*/
	glUnmapBuffer(GL_ARRAY_BUFFER);
	
	pos_attrib = glGetAttribLocation(beam_prog, "pos");
	assert(pos_attrib != -1);
	glEnableVertexAttribArray(pos_attrib);
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
	
	glUniform4f( glGetUniformLocation(beam_prog, "color"), 1, 1, 1, 1 );
	glUniformMatrix3fv( glGetUniformLocation(beam_prog, "to_norm"), 1, GL_FALSE, viewport->world_to_normal);
	
	glDrawArrays(GL_LINES, 0, unbroken_beam_count * 2);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

vec2_t particles_center(){
	vec2_t center = {0, 0};
	for(size_t i = 0; i < particle_count; i++){
		center.x += particles[i].pos.x / particle_count;
		center.y += particles[i].pos.y / particle_count;
	}
	return center;
}


//
// Thruster
//

GLuint thruster_prog, thruster_vertex_buffer;


void thrusters_load(){
	thruster_prog = load_and_link_program("thruster.vs", "thruster.ps");
	assert(thruster_prog != 0);
	
	glGenBuffers(1, &thruster_vertex_buffer);
	assert(thruster_vertex_buffer != 0);
	glBindBuffer(GL_ARRAY_BUFFER, thruster_vertex_buffer);
	
	const float vertecies[] = {
		0.5, 0.125,
		-0.5, 0.25,
		-0.5, -0.25,
		0.5, -0.125
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertecies), vertecies, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

}

void thrusters_unload(){
	free(thrusters);
	thrusters = NULL;
	thruster_count = 0;
	
	glDeleteBuffers(1, &thruster_vertex_buffer);
	delete_program_and_shaders(thruster_prog);
}

void thrusters_draw(){
	// Draw particles
	glUseProgram(thruster_prog);
	glBindBuffer(GL_ARRAY_BUFFER, thruster_vertex_buffer);
	
	GLint pos_attrib = glGetAttribLocation(thruster_prog, "pos");
	assert(pos_attrib != -1);
	glEnableVertexAttribArray(pos_attrib);
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
	
	GLint color_uni = glGetUniformLocation(thruster_prog, "color");
	GLint to_norm_uni = glGetUniformLocation(thruster_prog, "to_norm");
	GLint trans_uni = glGetUniformLocation(thruster_prog, "transform");
	assert(color_uni != -1);
	assert(to_norm_uni != -1);
	assert(trans_uni != -1);
	
	glUniformMatrix3fv(to_norm_uni, 1, GL_FALSE, viewport->world_to_normal);
	
	for(size_t i = 0; i < thruster_count; i++){
		thruster_p thruster = &thrusters[i];
		vec2_t p1_to_p2 = v2_sub(particles[thruster->p2_idx].pos, particles[thruster->p1_idx].pos);
		vec2_t pos = v2_add(particles[thruster->p1_idx].pos, v2_muls(p1_to_p2, 0.5));
		float rad = atan2f(p1_to_p2.y, p1_to_p2.x);
		float s = sin(rad), c = cos(rad);
		
		glUniformMatrix3fv(trans_uni, 1, GL_TRUE, (float[9]){
			c, -s, pos.x,
			s, c, pos.y,
			0, 0, 1
		});
		
		if (thruster->controlled_by & THRUSTER_BACK)
			glUniform4f(color_uni, 1, 1, 1, 1);
		else if (thruster->controlled_by & THRUSTER_FRONT)
			glUniform4f(color_uni, 0.5, 0.5, 0.5, 1);
		else if (thruster->controlled_by & THRUSTER_LEFT)
			glUniform4f(color_uni, 0.5, 0, 0, 1);
		else
			glUniform4f(color_uni, 0, 0.5, 0, 1);
		
		glDrawArrays(GL_QUADS, 0, 4);
	}
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

void thruster_add(size_t p1_idx, size_t p2_idx, float force, uint8_t controlled_by){
	thruster_count++;
	thrusters = realloc(thrusters, sizeof(thruster_t) * thruster_count);
	
	thrusters[thruster_count-1] = (thruster_t){
		.p1_idx = p1_idx,
		.p2_idx = p2_idx,
		.force = force,
		.controlled_by = controlled_by
	};
}



//
// Renderer
//
void renderer_resize(uint16_t window_width, uint16_t window_height){
	SDL_SetVideoMode(window_width, window_height, 24, SDL_OPENGL | SDL_RESIZABLE);
	glViewport(0, 0, window_width, window_height);
	vp_screen_changed(viewport, window_width, window_height);
}

void renderer_load(uint16_t window_width, uint16_t window_height, const char *title){
	// Create window
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_WM_SetCaption(title, NULL);
	
	// Initialize viewport structure
	viewport = vp_new((vec2_t){10, 10}, 2);
	renderer_resize(window_width, window_height);
	
	// Enable alpha blending
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

void renderer_unload(){
	vp_destroy(viewport);
}

void renderer_draw(){
	glClearColor(0, 0, 0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	
	grid_draw();
	particles_draw();
	thrusters_draw();
	cursor_draw();
}


//
// Simulation
//
ssize_t sim_grabbed_particle_idx = -1;
vec2_t sim_grabbed_force = {0, 0};
uint8_t sim_enabled_thrusters = 0;
bool sim_turbo = false;

typedef void (*particle_func_t)(particle_p particle, particle_p from);
typedef void (*beam_func_t)(beam_p beam, particle_p from, particle_p to);

/**
 * Do a broad iteration. That is iterate all connected beams before following the first beam to a new
 * particle. This is necessary for the forces to propagate from the start particle outwards. Otherwise
 * a depth first iteration might finish most particles "from behind" missing most of the forces caused
 * by the initial particle.
 */
void sim_traverse_particles(particle_p p, particle_func_t particle_func){
	assert(p != NULL && particle_func != NULL);
	
	particle_func(p, NULL);
	p->flags |= PARTICLE_TRAVERSED;
	
	size_t traversed_particles;
	do {
		traversed_particles = 0;
		
		for(size_t i = 0; i < beam_count; i++){
			beam_p beam = &beams[i];
			
			if ( (beam->p1->flags & PARTICLE_TRAVERSED) && !(beam->p2->flags & PARTICLE_TRAVERSED) ) {
				particle_func(beam->p2, beam->p1);
				beam->p2->flags |= PARTICLE_TRAVERSED;
				traversed_particles++;
			} else if ( (beam->p2->flags & PARTICLE_TRAVERSED) && !(beam->p1->flags & PARTICLE_TRAVERSED) ) {
				particle_func(beam->p1, beam->p2);
				beam->p1->flags |= PARTICLE_TRAVERSED;
				traversed_particles++;
			}
		}
	} while (traversed_particles > 0);
	
	/*
	for(size_t i = 0; i < beam_count; i++){
		beam_p beam = &beams[i];
		if ( (beam->flags & BEAM_TRAVERSED) == 0 ) {
			if (beam->p1 == p) {
				beam_func(beam, p, beam->p2);
				beam->flags |= BEAM_TRAVERSED;
			} else if (beam->p2 == p) {
				beam_func(beam, p, beam->p1);
				beam->flags |= BEAM_TRAVERSED;
			}
		}
	}
	
	for(size_t i = 0; i < beam_count; i++){
		beam_p beam = &beams[i];
		if ( ((beam->flags & BEAM_FOLLOWED) == 0) && (beam->p1 == p || beam->p2 == p) ){
			beam->flags |= BEAM_FOLLOWED;
			sim_traverse( (beam->p1 == p ? beam->p2 : beam->p1), beam_func, particle_func);
		}
	}
	
	particle_func(p);
	*/
}

void sim_traverse_beams(particle_p p, bool mark, beam_func_t beam_func){
	assert(p != NULL && beam_func != NULL);
	
	for(size_t i = 0; i < beam_count; i++){
		beam_p beam = &beams[i];
		if ( !(beam->flags & BEAM_TRAVERSED) ) {
			if (beam->p1 == p) {
				beam_func(beam, p, beam->p2);
				if (mark)
					beam->flags |= BEAM_TRAVERSED;
			} else if (beam->p2 == p) {
				beam_func(beam, p, beam->p1);
				if (mark)
					beam->flags |= BEAM_TRAVERSED;
			}
		}
	}	
}

void sim_clear_traverse_flags(){
	for(size_t i = 0; i < beam_count; i++)
		beams[i].flags = 0;
	for(size_t i = 0; i < particle_count; i++)
		particles[i].flags = 0;
}

void sim_propagate_forces(){
	sim_clear_traverse_flags();
	
	void particle_func(particle_p particle, particle_p particle_from){
		printf("iterating particle %p\n", particle);
		
		float scalar_sum = 0;
		void scalar_summer(beam_p beam, particle_p from, particle_p to){
			scalar_sum += v2_sprod( v2_norm(v2_sub(to->pos, from->pos)), v2_norm(particle->force) );
		}
		sim_traverse_beams(particle, false, scalar_summer);
		printf("scalar sum: %f\n", scalar_sum);
		
		if (scalar_sum > 0){
			void beam_func(beam_p beam, particle_p from, particle_p to){
				printf("iterating beam from %p to %p\n", from, to);
				float proj = v2_sprod( v2_norm(v2_sub(to->pos, from->pos)), v2_norm(particle->force) );
				to->force = v2_muls(particle->force, proj / scalar_sum);
			}
			sim_traverse_beams(particle, true, beam_func);
		}
		
		if (particle_from != NULL)
			particle->force = particle_from->force;
	}
	
	sim_traverse_particles(&particles[sim_grabbed_particle_idx], particle_func);
}

/**
 * dt in seconds.
 */
void simulate(float dt){
	if (debug) printf("step with dt %fs\n", dt);
	
	for(size_t i = 0; i < particle_count; i++)
		particles[i].force = (vec2_t){0, 0};
	
	if (sim_grabbed_particle_idx != -1)
		particles[sim_grabbed_particle_idx].force = v2_add(particles[sim_grabbed_particle_idx].force, v2_muls(sim_grabbed_force, 10));
	
	// Iterate over all thrusters and apply the thruster force to all connected particles
	if (debug) printf("  thrusters: %02x\n", sim_enabled_thrusters);
	for(size_t i = 0; i < thruster_count; i++){
		thruster_p t = &thrusters[i];
		//if (debug) printf("  thruster: cb %02x, et %2x result %d\n", t->controlled_by, sim_enabled_thrusters, (sim_enabled_thrusters & t->controlled_by));
		if ( !(sim_enabled_thrusters & t->controlled_by) )
			continue;
		
		vec2_t force_dir = v2_norm( v2_sub(particles[t->p2_idx].pos, particles[t->p1_idx].pos) );
		float force_mag = t->force;
		// Turbo only for main thrusters. Otherwise turbo rotation tares the ship apart for sure.
		if (sim_turbo && (t->controlled_by & THRUSTER_BACK))
			force_mag *= 5;
		vec2_t force = v2_muls(force_dir, force_mag);
		
		particles[t->p1_idx].force = v2_add(particles[t->p1_idx].force, force);
		particles[t->p2_idx].force = v2_add(particles[t->p2_idx].force, force);
	}
	
	
	// Iterate all beams and calculate the forces they exert on the particles
	float modulus_of_elasticity = 50000; // 210e3; // 210e9; // N_m2 (elastic modulus of steel)
	float beam_profile_area = 0.2 * 0.2; // m2
	float deform_threshold = 0.05; // m
	float break_threshold = 0.075; // m
	for(size_t i = 0; i < beam_count; i++){
		beam_p beam = &beams[i];
		
		if (beam->flags & BEAM_BROKEN)
			continue;
		
		vec2_t p1_to_p2 = v2_sub(beam->p2->pos, beam->p1->pos);
		float p1_to_p2_len = v2_length(p1_to_p2);
		
		float dilatation = beam->length - p1_to_p2_len;
		float spring_constant = (modulus_of_elasticity * beam_profile_area) / beam->length;
		float force = spring_constant * dilatation;
		if (debug) printf("  beam %8.2f m, dl %6.2f m, force: %8.2f N", beam->length, dilatation, force);
		
		if (dilatation > break_threshold) {
			beam->flags |= BEAM_BROKEN;
			continue;
		} else if (dilatation > deform_threshold) {
			beam->length -= force / (modulus_of_elasticity * beam_profile_area) * beam->length;
			if (beam->length < 0)
				beam->length = 0;
			if (debug) printf(" deformed to %8.2f m (force %8.2f N)", beam->length, force);
		}
		if (debug) printf("\n");
		
		vec2_t p1_to_p2_norm = v2_divs(p1_to_p2, p1_to_p2_len);
		beam->p1->force = v2_add(beam->p1->force, v2_muls(p1_to_p2_norm, -force));
		beam->p2->force = v2_add(beam->p2->force, v2_muls(p1_to_p2_norm, force));
	}
	
	// Iterate over all particles to advance to the next time step. Delete all forces afterwards.
	for(size_t i = 0; i < particle_count; i++){
		/*
		a = f / m;
		v = v + a * dt;
		s = s + v * dt;
		*/
		particle_p p = &particles[i];
		
		vec2_t acl;
		acl.x = p->force.x / p->mass;
		acl.y = p->force.y / p->mass;
		p->vel.x += acl.x * dt;
		p->vel.y += acl.y * dt;
		p->pos.x += p->vel.x * dt;
		p->pos.y += p->vel.y * dt;
		
		p->force = (vec2_t){0, 0};
	}
}

typedef struct  {
	particle_p particle;
	size_t index;
	float dist;
	vec2_t to_particle;
} closest_particle_t;

closest_particle_t sim_nearest_particle(vec2_t pos){
	size_t closest_idx = 0;
	float closest_dist = INFINITY;
	vec2_t to_closest;
	for(size_t i = 0; i < particle_count; i++){
		vec2_t to_particle = v2_sub(particles[i].pos, pos);
		float dist = v2_length(to_particle);
		if (dist < closest_dist){
			closest_idx = i;
			closest_dist = dist;
			to_closest = to_particle;
		}
	}
	
	return (closest_particle_t){ &particles[closest_idx], closest_idx, closest_dist, to_closest };
}

void sim_apply_force(){
	vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
	
	// Find nearest particle
	closest_particle_t cp = sim_nearest_particle(world_cursor);
	
	sim_grabbed_particle_idx = cp.index;
	sim_grabbed_force = cp.to_particle;
}

void sim_retain_force(){
	sim_grabbed_particle_idx = -1;
	sim_grabbed_force = (vec2_t){0, 0};
}

enum prog_mode_e { MODE_EDIT, MODE_SIM };
typedef enum prog_mode_e prog_mode_t;

int main(int argc, char **argv){
	if (argc != 3){
		fprintf(stderr, "usage: %s load.mesh save.mesh\n", argv[0]);
		return 1;
	}
	
	uint32_t cycle_duration = 10.0; //1.0 / 60.0 * 1000;
	uint16_t win_w = 640, win_h = 480;
	
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	renderer_load(win_w, win_h, "Grid");
	
	grid_load();
	cursor_load();
	particles_load();
	thrusters_load();
	particles_load_model(argv[1]);
	
	SDL_Event e;
	bool quit = false, viewport_grabbed = false, paused = false, follow = false;
	uint32_t ticks = SDL_GetTicks();
	
	prog_mode_t mode = MODE_SIM;
	ssize_t selected_particles_idx[2] = {-1};
	float default_thruster_force = 10;
	
	while (!quit) {
		while ( SDL_PollEvent(&e) ) {
			switch(e.type){
				case SDL_QUIT:
					quit = true;
					break;
				case SDL_VIDEORESIZE:
					renderer_resize(e.resize.w, e.resize.h);
					break;
				case SDL_KEYDOWN:
					switch(e.key.keysym.sym){
						case SDLK_LEFT:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_enabled_thrusters |= THRUSTER_LEFT;
							}
							break;
						case SDLK_RIGHT:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_enabled_thrusters |= THRUSTER_RIGHT;
							}
							break;
						case SDLK_UP:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_enabled_thrusters |= THRUSTER_BACK;
							}
							break;
						case SDLK_DOWN:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_enabled_thrusters |= THRUSTER_FRONT;
							}
							break;
						case SDLK_RSHIFT: case SDLK_LSHIFT:
							sim_turbo = true;
							break;
					}
					break;
				case SDL_KEYUP:
					switch(e.key.keysym.sym){
						case SDLK_SPACE:
							paused = !paused;
							if (paused)
								printf("simulation paused\n");
							else
								printf("continuing simulation\n");
							break;
						case SDLK_l:
							// If shift is pressed load the save file mesh, otherwise the load file mesh
							if ( (e.key.keysym.mod & KMOD_RSHIFT) || (e.key.keysym.mod & KMOD_LSHIFT) )
								particles_load_model(argv[2]);
							else
								particles_load_model(argv[1]);
							selected_particles_idx[0] = -1;
							selected_particles_idx[1] = -1;
							break;
						case SDLK_s:
							particles_save_model(argv[2]);
							break;
						case SDLK_m:
							if (mode == MODE_EDIT) {
								mode = MODE_SIM;
								printf("switched to simulation mode\n");
							} else {
								mode = MODE_EDIT;
								printf("switched to edit mode\n");
							}
							break;
						case SDLK_b:  // create beam
							particles_add_beam(&particles[selected_particles_idx[0]], &particles[selected_particles_idx[1]]);
							printf("beam from particle %zu to %zu\n", selected_particles_idx[0], selected_particles_idx[1]);
							goto deselect;
							break;
						case SDLK_n:  // select none (deselect particles)
							deselect:
							if (selected_particles_idx[0] != -1){
								particles[selected_particles_idx[0]].flags &= ~PARTICLE_SELECTED;
								selected_particles_idx[0] = -1;
							}
							if (selected_particles_idx[1] != -1){
								particles[selected_particles_idx[1]].flags &= ~PARTICLE_SELECTED;
								selected_particles_idx[1] = -1;
							}
							break;
						case SDLK_f:
							follow = !follow;
							break;
						case SDLK_d:
							debug = !debug;
							break;
						case SDLK_c:
							simulate(cycle_duration / 1000.0);
							break;
						case SDLK_LEFT:
							if (mode == MODE_EDIT) {
								thruster_add(selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_LEFT);
								goto deselect;
							} else {
								sim_enabled_thrusters &= ~THRUSTER_LEFT;
							}
							break;
						case SDLK_RIGHT:
							if (mode == MODE_EDIT) {
								thruster_add(selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_RIGHT);
								goto deselect;
							} else {
								sim_enabled_thrusters &= ~THRUSTER_RIGHT;
							}
							break;
						case SDLK_UP:
							if (mode == MODE_EDIT) {
								thruster_add(selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_BACK);
								goto deselect;
							} else {
								sim_enabled_thrusters &= ~THRUSTER_BACK;
							}
							break;
						case SDLK_DOWN:
							if (mode == MODE_EDIT) {
								thruster_add(selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_FRONT);
								goto deselect;
							} else {
								sim_enabled_thrusters &= ~THRUSTER_FRONT;
							}
							break;
						case SDLK_RSHIFT: case SDLK_LSHIFT:
							sim_turbo = false;
							break;
					}
					break;
				case SDL_MOUSEMOTION:
					cursor_pos.x = e.motion.x;
					cursor_pos.y = e.motion.y;
					
					//vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
					//printf("world cursor: %f %f\n", world_cursor.x, world_cursor.y);
					//particles[0].pos = world_cursor;
					
					if (viewport_grabbed){
						// Only use the scaling factors from the current screen to world matrix. Since we work
						// with deltas here the offsets are not necessary (in fact would destroy the result).
						viewport->pos.x += -viewport->screen_to_world[0] * e.motion.xrel;
						viewport->pos.y += -viewport->screen_to_world[4] * e.motion.yrel;
						vp_changed(viewport);
					}
					
					break;
				case SDL_MOUSEBUTTONDOWN:
					switch(e.button.button){
						case SDL_BUTTON_LEFT:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_apply_force();
							}
							break;
						case SDL_BUTTON_MIDDLE:
							viewport_grabbed = true;
							break;
						case SDL_BUTTON_RIGHT:
							break;
						case SDL_BUTTON_WHEELUP:
							break;
						case SDL_BUTTON_WHEELDOWN:
							break;
					}
					break;
				case SDL_MOUSEBUTTONUP:
					switch(e.button.button){
						case SDL_BUTTON_LEFT:
							if (mode == MODE_EDIT) {
								vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
								closest_particle_t cp = sim_nearest_particle(world_cursor);
								
								if (selected_particles_idx[0] == -1) {
									selected_particles_idx[0] = cp.index;
									cp.particle->flags |= PARTICLE_SELECTED;
								} else if (selected_particles_idx[1] == -1) {
									selected_particles_idx[1] = cp.index;
									cp.particle->flags |= PARTICLE_SELECTED;
								}
								/*
								if (selected_particle == NULL) {
									// Nothing selected yet, select closest particle
									vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
									selected_particle = sim_nearest_particle(world_cursor).particle;
									cursor_color = (color_t){1, 0, 0, 1};
									printf("selected particle %zu\n", selected_particle - particles);
								} else {
									// Build a beam between the selected particle and the one closest to the cursor
									vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
									particle_p target_particle = sim_nearest_particle(world_cursor).particle;
									particles_add_beam(selected_particle, target_particle);
									printf("beam from particle %zu to %zu\n", selected_particle - particles, target_particle - particles);
									cursor_color = (color_t){1, 1, 1, 1};
									selected_particle = NULL;
								}
								*/
							} else {
								sim_retain_force();
							}
							break;
						case SDL_BUTTON_MIDDLE:
							viewport_grabbed = false;
							break;
						case SDL_BUTTON_RIGHT:
							if (mode == MODE_EDIT) {
								// Create new particle at world pos
								vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
								particles_add(world_cursor.x, world_cursor.y, 1);
							}
							break;
						case SDL_BUTTON_WHEELUP:
							viewport->scale_exp -= 0.1;
							{
								vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
								float new_scale = vp_scale_for(viewport, viewport->scale_exp);
								viewport->pos = (vec2_t){
									world_cursor.x + (viewport->pos.x - world_cursor.x) * (new_scale / viewport->scale),
									world_cursor.y + (viewport->pos.y - world_cursor.y) * (new_scale / viewport->scale)
								};
							}
							vp_changed(viewport);
							break;
						case SDL_BUTTON_WHEELDOWN:
							viewport->scale_exp += 0.1;
							{
								vec2_t world_cursor = m3_v2_mul(viewport->screen_to_world, cursor_pos);
								float new_scale = vp_scale_for(viewport, viewport->scale_exp);
								viewport->pos = (vec2_t){
									world_cursor.x + (viewport->pos.x - world_cursor.x) * (new_scale / viewport->scale),
									world_cursor.y + (viewport->pos.y - world_cursor.y) * (new_scale / viewport->scale)
								};
							}
							vp_changed(viewport);
							break;
					}
					break;
			}
		}
		
		if (follow){
			viewport->pos = particles_center();
			vp_changed(viewport);
		}
		
		renderer_draw();
		SDL_GL_SwapBuffers();
		if (mode == MODE_SIM && !paused)
			simulate(cycle_duration / 1000.0);
		
		int32_t duration = cycle_duration - (SDL_GetTicks() - ticks);
		if (duration > 0)
			SDL_Delay(duration);
		ticks = SDL_GetTicks();
	}
	
	// Cleanup time
	thrusters_unload();
	particles_unload();
	cursor_unload();
	grid_unload();
	renderer_unload();
	
	SDL_Quit();
	return 0;
}