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
#include "model.h"



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
model_p player = NULL;

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
GLuint particle_prog, particle_vertex_buffer;
GLuint beam_prog, beam_vertex_buffer;

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
	
	for(size_t i = 0; i < player->particle_count; i++){
		glUniformMatrix3fv(trans_uni, 1, GL_TRUE, (float[9]){
			0.25, 0, player->particles[i].pos.x,
			0, 0.25, player->particles[i].pos.y,
			0, 0, 1
		});
		
		if (player->particles[i].flags & PARTICLE_SELECTED)
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
	for(size_t i = 0; i < player->beam_count; i++){
		if ( !(player->beams[i].flags & BEAM_BROKEN) )
			unbroken_beam_count++;
	}
	
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * unbroken_beam_count, NULL, GL_STATIC_DRAW);
	float *vertex_buffer = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
	size_t vi = 0;
	for(size_t i = 0; i < player->beam_count; i++){
		beam_p b = &player->beams[i];
		
		if (b->flags & BEAM_BROKEN)
			continue;
		
		vertex_buffer[vi*4+0] = player->particles[b->i1].pos.x;
		vertex_buffer[vi*4+1] = player->particles[b->i1].pos.y;
		vertex_buffer[vi*4+2] = player->particles[b->i2].pos.x;
		vertex_buffer[vi*4+3] = player->particles[b->i2].pos.y;
		vi++;
	}
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
	
	for(size_t i = 0; i < player->thruster_count; i++){
		thruster_p thruster = &player->thrusters[i];
		
		vec2_t p1_to_p2 = v2_sub(player->particles[thruster->i2].pos, player->particles[thruster->i1].pos);
		vec2_t pos = v2_add(player->particles[thruster->i1].pos, v2_muls(p1_to_p2, 0.5));
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

/**
 * dt in seconds.
 */
void simulate(float dt){
	if (debug) printf("step with dt %fs\n", dt);
	
	//for(size_t i = 0; i < player->particle_count; i++)
	//	player->particles[i].force = (vec2_t){0, 0};
	
	if (sim_grabbed_particle_idx != -1)
		player->particles[sim_grabbed_particle_idx].force = v2_add(player->particles[sim_grabbed_particle_idx].force, v2_muls(sim_grabbed_force, 10));
	
	// Iterate over all thrusters and apply the thruster force to all connected particles
	if (debug) printf("  thrusters: %02x\n", sim_enabled_thrusters);
	for(size_t i = 0; i < player->thruster_count; i++){
		thruster_p t = &player->thrusters[i];
		//if (debug) printf("  thruster: cb %02x, et %2x result %d\n", t->controlled_by, sim_enabled_thrusters, (sim_enabled_thrusters & t->controlled_by));
		if ( !(sim_enabled_thrusters & t->controlled_by) )
			continue;
		
		vec2_t force_dir = v2_norm( v2_sub(player->particles[t->i2].pos, player->particles[t->i1].pos) );
		float force_mag = t->force;
		// Turbo only for main thrusters. Otherwise turbo rotation tares the ship apart for sure.
		if (sim_turbo && (t->controlled_by & THRUSTER_BACK))
			force_mag *= 5;
		vec2_t force = v2_muls(force_dir, force_mag);
		
		player->particles[t->i1].force = v2_add(player->particles[t->i1].force, force);
		player->particles[t->i2].force = v2_add(player->particles[t->i2].force, force);
	}
	
	
	// Iterate all beams and calculate the forces they exert on the particles
	float modulus_of_elasticity = 50000; // 210e3; // 210e9; // N_m2 (elastic modulus of steel)
	float beam_profile_area = 0.2 * 0.2; // m2
	float deform_threshold = 0.05; // m
	float break_threshold = 0.075; // m
	for(size_t i = 0; i < player->beam_count; i++){
		beam_p beam = &player->beams[i];
		
		if (beam->flags & BEAM_BROKEN)
			continue;
		
		vec2_t p1_to_p2 = v2_sub(player->particles[beam->i2].pos, player->particles[beam->i1].pos);
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
		player->particles[beam->i1].force = v2_add(player->particles[beam->i1].force, v2_muls(p1_to_p2_norm, -force));
		player->particles[beam->i2].force = v2_add(player->particles[beam->i2].force, v2_muls(p1_to_p2_norm, force));
	}
	
	// Iterate over all particles to advance to the next time step. Delete all forces afterwards.
	for(size_t i = 0; i < player->particle_count; i++){
		/*
		a = f / m;
		v = v + a * dt;
		s = s + v * dt;
		*/
		particle_p p = &player->particles[i];
		
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
	for(size_t i = 0; i < player->particle_count; i++){
		vec2_t to_particle = v2_sub(player->particles[i].pos, pos);
		float dist = v2_length(to_particle);
		if (dist < closest_dist){
			closest_idx = i;
			closest_dist = dist;
			to_closest = to_particle;
		}
	}
	
	return (closest_particle_t){ &player->particles[closest_idx], closest_idx, closest_dist, to_closest };
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
	if (argc != 2){
		fprintf(stderr, "usage: %s load.mesh\n", argv[0]);
		return 1;
	}
	
	const char *save_mesh = "save.mesh";
	uint32_t cycle_duration = 10.0; //1.0 / 60.0 * 1000;
	uint16_t win_w = 640, win_h = 480;
	
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	renderer_load(win_w, win_h, "Grid");
	
	grid_load();
	cursor_load();
	particles_load();
	thrusters_load();
	
	player = model_new();
	model_load(player, argv[1]);
	
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
						case SDLK_a:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_enabled_thrusters |= THRUSTER_LEFT;
							}
							break;
						case SDLK_d:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_enabled_thrusters |= THRUSTER_RIGHT;
							}
							break;
						case SDLK_w:
							if (mode == MODE_EDIT) {
								// ...
							} else {
								sim_enabled_thrusters |= THRUSTER_BACK;
							}
							break;
						case SDLK_s:
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
								model_load(player, save_mesh);
							else
								model_load(player, argv[1]);
							selected_particles_idx[0] = -1;
							selected_particles_idx[1] = -1;
							break;
						case SDLK_k:
							model_save(player, save_mesh);
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
							model_add_beam(player, selected_particles_idx[0], selected_particles_idx[1]);
							printf("beam from particle %zu to %zu\n", selected_particles_idx[0], selected_particles_idx[1]);
							goto deselect;
							break;
						case SDLK_n:  // select none (deselect particles)
							deselect:
							if (selected_particles_idx[0] != -1){
								player->particles[selected_particles_idx[0]].flags &= ~PARTICLE_SELECTED;
								selected_particles_idx[0] = -1;
							}
							if (selected_particles_idx[1] != -1){
								player->particles[selected_particles_idx[1]].flags &= ~PARTICLE_SELECTED;
								selected_particles_idx[1] = -1;
							}
							break;
						case SDLK_f:
							follow = !follow;
							break;
						case SDLK_v:  // verbose / debugging
							debug = !debug;
							break;
						case SDLK_c:
							simulate(cycle_duration / 1000.0);
							break;
						case SDLK_a:
							if (mode == MODE_EDIT) {
								model_add_thruster(player, selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_LEFT);
								goto deselect;
							} else {
								sim_enabled_thrusters &= ~THRUSTER_LEFT;
							}
							break;
						case SDLK_d:
							if (mode == MODE_EDIT) {
								model_add_thruster(player, selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_RIGHT);
								goto deselect;
							} else {
								sim_enabled_thrusters &= ~THRUSTER_RIGHT;
							}
							break;
						case SDLK_w:
							if (mode == MODE_EDIT) {
								model_add_thruster(player, selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_BACK);
								goto deselect;
							} else {
								sim_enabled_thrusters &= ~THRUSTER_BACK;
							}
							break;
						case SDLK_s:
							if (mode == MODE_EDIT) {
								model_add_thruster(player, selected_particles_idx[0], selected_particles_idx[1], default_thruster_force, THRUSTER_FRONT);
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
								model_add_particle(player, world_cursor.x, world_cursor.y, 1);
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
			viewport->pos = model_particle_center(player);
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
	model_destroy(player);
	thrusters_unload();
	particles_unload();
	cursor_unload();
	grid_unload();
	renderer_unload();
	
	SDL_Quit();
	return 0;
}