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

// Forward declarations for various things
vec2_t screen_size;
mat3_t screen_to_normal_mat, screen_to_world_mat;
mat3_t world_to_normal_mat, world_to_screen_mat;

vec2_t vp_pos = {0, 0};

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
		grid_default_spacing.x * world_to_screen_mat[0],
		grid_default_spacing.y * world_to_screen_mat[4]
	};
	vec2_t grid_offset = (vec2_t){
		fmodf(vp_pos.x, grid_default_spacing.x) * world_to_screen_mat[0] - fmodf(screen_size.x / 2, grid_spacing.x),
		fmodf(vp_pos.y, grid_default_spacing.y) * world_to_screen_mat[4] - fmodf(screen_size.y / 2, grid_spacing.y)
	};
	
	glUseProgram(grid_prog);
	glBindBuffer(GL_ARRAY_BUFFER, grid_vertex_buffer);
	
	GLint pos_attrib = glGetAttribLocation(grid_prog, "pos");
	glEnableVertexAttribArray(pos_attrib);
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
	
	glUniform4f( glGetUniformLocation(grid_prog, "color"), 0, 0, 0.5, 1 );
	glUniform2f( glGetUniformLocation(grid_prog, "grid_spacing"), grid_spacing.x, grid_spacing.y );
	glUniform2f( glGetUniformLocation(grid_prog, "grid_offset"), grid_offset.x, grid_offset.y );
	glDrawArrays(GL_QUADS, 0, 4);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}


//
// Cursor
//
vec2_t cursor_pos = {0, 0};
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
	glUniform4f(color_uni, 1, 1, 1, 1);
	
	GLint projection_uni = glGetUniformLocation(cursor_prog, "projection");
	assert(projection_uni != -1);
	glUniformMatrix3fv(projection_uni , 1, GL_FALSE, screen_to_normal_mat);
	
	glDrawArrays(GL_QUADS, 0, 4);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}


//
// Particles
//
typedef struct {
	vec2_t pos, vel, force;
	float mass;
} particle_t, *particle_p;

particle_t *particles = NULL;
size_t particle_count;
GLuint particle_prog, particle_vertex_buffer;

void particles_load(){
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
	
	// Create test particles
	srand(5);
	particle_count = 4;
	particles = realloc(particles, sizeof(particle_t) * particle_count);
	
	particles[0] = (particle_t){
		.pos = (vec2_t){ -1, -1 },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = 1
	};
	particles[1] = (particle_t){
		.pos = (vec2_t){ 0, 0 },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = 1
	};
	particles[2] = (particle_t){
		.pos = (vec2_t){ 2, 2 },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = 1
	};
	particles[3] = (particle_t){
		.pos = (vec2_t){ 2, 0 },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = 1
	};
	
	/*
	for(size_t i = 0; i < particle_count; i++){
		particles[i] = (particle_t){
			.pos = (vec2_t){ rand_in(-10, 10), rand_in(-10, 10) },
			.vel = (vec2_t){ rand_in(-2, 2), rand_in(-2, 2) },
			.force = (vec2_t){0, 0},
			.mass = 1
		};
	}
	*/
}

void particles_unload(){
	free(particles);
	particles = NULL;
	
	glDeleteBuffers(1, &particle_vertex_buffer);
	delete_program_and_shaders(particle_prog);
}

void particles_draw(){
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
	
	glUniform4f(color_uni, 0, 1, 0, 1 );
	glUniformMatrix3fv(to_norm_uni, 1, GL_FALSE, world_to_normal_mat);
	
	for(size_t i = 0; i < particle_count; i++){
		glUniformMatrix3fv(trans_uni, 1, GL_TRUE, (float[9]){
			1, 0, particles[i].pos.x,
			0, 1, particles[i].pos.y,
			0, 0, 1
		});
		glDrawArrays(GL_QUADS, 0, 4);
	}
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}


//
// Camera
//

// Default min width and min height of viewport. These are the dimensions of the viewport with
// no scaling applied. Min width is used in landscape mode, min height in portrait mode.
vec2_t vp_default_size = { 10, 10 };
// Center of viewport, declaration moved to the top
//vec2_t vp_pos = {0, 0};
// Size of the viewport in world coords
vec2_t vp_size;
// Viewport scaling
float vp_scale_base = 2, vp_scale_exp = 0, vp_scale = 1;
bool vp_grabbed = false;

float vp_scale_for(float exp){
	return powf(vp_scale_base, exp);
}

void cam_update(){
	float aspect_ratio = screen_size.x / screen_size.y;
	vp_scale = vp_scale_for(vp_scale_exp);
	//printf("viewport: aspect ratio %f, scale base: %f, scale exp: %f, scale %f\n", aspect_ratio, vp_scale_base, vp_scale_exp, vp_scale);
	
	if (aspect_ratio > 1) {
		// Landscape format, use vp_default_size.y as minimal height
		vp_size.y = vp_default_size.y * vp_scale;
		vp_size.x = vp_size.y * aspect_ratio;
	} else {
		// Portrait format, use vp_default_size.x as minimal width
		vp_size.x = vp_default_size.x * vp_scale;
		vp_size.y = vp_size.x / aspect_ratio;
	}
	
	// Calculate matrix to convert screen coords back to world coords
	float sx = vp_size.x / screen_size.x;
	float sy = -vp_size.y / screen_size.y;
	float tx = -0.5 * vp_size.x + vp_pos.x;
	float ty = 0.5 * vp_size.y + vp_pos.y;
	m3_transpose(screen_to_world_mat, (mat3_t){
		sx, 0, tx,
		0, sy, ty,
		0, 0, 1
	});
	
	sx = 2 / vp_size.x;
	sy = 2 / vp_size.y;
	tx = -vp_pos.x * (2 / vp_size.x);
	ty = -vp_pos.y * (2 / vp_size.y);
	m3_transpose(world_to_normal_mat, (mat3_t){
		sx, 0, tx,
		0, sy, ty,
		0, 0, 1
	});
	
	sx = screen_size.x / vp_size.x;
	sy = screen_size.y / vp_size.y;
	tx = -vp_pos.x * sx + 0.5 * screen_size.x;
	ty = -vp_pos.y * sy + 0.5 * screen_size.y;
	m3_transpose(world_to_screen_mat, (mat3_t){
		sx, 0, tx,
		0, sy, ty,
		0, 0, 1
	});
}

void cam_load(){
	cam_update();
}


//
// Renderer
//
void renderer_resize(uint16_t window_width, uint16_t window_height){
	screen_size = (vec2_t){ window_width, window_height };
	SDL_SetVideoMode(window_width, window_height, 24, SDL_OPENGL | SDL_RESIZABLE);
	glViewport(0, 0, window_width, window_height);
	
	float sx = 2.0 / screen_size.x;
	float sy = -2.0 / screen_size.y;
	float tx = -1.0;
	float ty = 1.0;
	m3_transpose(screen_to_normal_mat, (mat3_t){
		sx, 0, tx,
		0, sy, ty,
		0, 0, 1
	});
	
	cam_update();
}

void renderer_load(uint16_t window_width, uint16_t window_height, const char *title){
	// Create window
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_WM_SetCaption(title, NULL);
	renderer_resize(window_width, window_height);
	
	// Enable alpha blending
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

void renderer_unload(){
}

void renderer_draw(){
	glClearColor(0, 0, 0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	
	grid_draw();
	particles_draw();
	cursor_draw();
}


//
// Simulation
//

void simulate(float dt){
}



int main(int argc, char **argv){
	uint32_t cycle_duration = 1.0 / 60.0 * 1000;
	uint16_t win_w = 640, win_h = 480;
	
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	renderer_load(win_w, win_h, "Grid");
	
	grid_load();
	cursor_load();
	cam_load();
	particles_load();
	
	SDL_Event e;
	bool quit = false;
	uint32_t ticks = SDL_GetTicks();
	
	while (!quit) {
		while ( SDL_PollEvent(&e) ) {
			switch(e.type){
				case SDL_QUIT:
					quit = true;
					break;
				case SDL_VIDEORESIZE:
					renderer_resize(e.resize.w, e.resize.h);
					break;
				case SDL_KEYUP:
					switch(e.key.keysym.sym){
						case SDLK_LEFT:
							vp_pos.x -= 1;
							cam_update();
							break;
						case SDLK_RIGHT:
							vp_pos.x += 1;
							cam_update();
							break;
						case SDLK_UP:
							vp_pos.y += 1;
							cam_update();
							break;
						case SDLK_DOWN:
							vp_pos.y -= 1;
							cam_update();
							break;
					}
					break;
				case SDL_MOUSEMOTION:
					cursor_pos.x = e.motion.x;
					cursor_pos.y = e.motion.y;
					
					vec2_t world_cursor = m3_v2_mul(screen_to_world_mat, cursor_pos);
					//printf("world cursor: %f %f\n", world_cursor.x, world_cursor.y);
					//particles[0].pos = world_cursor;
					
					if (vp_grabbed){
						// Only use the scaling factors from the current screen to world matrix. Since we work
						// with deltas here the offsets are not necessary (in fact would destroy the result).
						vp_pos.x += -screen_to_world_mat[0] * e.motion.xrel;
						vp_pos.y += -screen_to_world_mat[4] * e.motion.yrel;
						cam_update();
					}
					
					break;
				case SDL_MOUSEBUTTONDOWN:
					switch(e.button.button){
						case SDL_BUTTON_LEFT:
							break;
						case SDL_BUTTON_MIDDLE:
							vp_grabbed = true;
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
							break;
						case SDL_BUTTON_MIDDLE:
							vp_grabbed = false;
							break;
						case SDL_BUTTON_RIGHT:
							break;
						case SDL_BUTTON_WHEELUP:
							vp_scale_exp -= 0.1;
							{
								vec2_t world_cursor = m3_v2_mul(screen_to_world_mat, cursor_pos);
								float new_scale = vp_scale_for(vp_scale_exp);
								vp_pos = (vec2_t){
									world_cursor.x + (vp_pos.x - world_cursor.x) * (new_scale / vp_scale),
									world_cursor.y + (vp_pos.y - world_cursor.y) * (new_scale / vp_scale)
								};
							}
							cam_update();
							break;
						case SDL_BUTTON_WHEELDOWN:
							vp_scale_exp += 0.1;
							{
								vec2_t world_cursor = m3_v2_mul(screen_to_world_mat, cursor_pos);
								float new_scale = vp_scale_for(vp_scale_exp);
								vp_pos = (vec2_t){
									world_cursor.x + (vp_pos.x - world_cursor.x) * (new_scale / vp_scale),
									world_cursor.y + (vp_pos.y - world_cursor.y) * (new_scale / vp_scale)
								};
							}
							cam_update();
							break;
					}
					break;
			}
		}
		
		renderer_draw();
		SDL_GL_SwapBuffers();
		simulate(cycle_duration / 1000.0);
		
		int32_t duration = cycle_duration - (SDL_GetTicks() - ticks);
		if (duration > 0)
			SDL_Delay(duration);
		ticks = SDL_GetTicks();
	}
	
	// Cleanup time
	particles_unload();
	cursor_unload();
	grid_unload();
	renderer_unload();
	
	SDL_Quit();
	return 0;
}