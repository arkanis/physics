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
vec2_t renderer_screen_size;
mat3_t world_to_normal_mat, screen_to_normal_mat, screen_to_world_mat;


//
// Grid
//
GLuint grid_prog, grid_vertex_buffer;

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
	glUseProgram(grid_prog);
	glBindBuffer(GL_ARRAY_BUFFER, grid_vertex_buffer);
	
	GLint pos_attrib = glGetAttribLocation(grid_prog, "pos");
	glEnableVertexAttribArray(pos_attrib);
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
	
	glUniform4f( glGetUniformLocation(grid_prog, "color"), 1, 1, 1, 1 );
	glUniform2f( glGetUniformLocation(grid_prog, "origin"), 25, 25 );
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
	particle_count = 10;
	particles = realloc(particles, sizeof(particle_t) * particle_count);
	/*
	particles[0] = (particle_t){
		.pos = (vec2_t){ 0, 0 },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = 1
	};
	particles[1] = (particle_t){
		.pos = (vec2_t){ 2, 2 },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = 1
	};
	particles[2] = (particle_t){
		.pos = (vec2_t){ 2, 0 },
		.vel = (vec2_t){ 0, 0 },
		.force = (vec2_t){0, 0},
		.mass = 1
	};
	*/
	
	for(size_t i = 0; i < particle_count; i++){
		particles[i] = (particle_t){
			.pos = (vec2_t){ rand_in(-10, 10), rand_in(-10, 10) },
			.vel = (vec2_t){ rand_in(-2, 2), rand_in(-2, 2) },
			.force = (vec2_t){0, 0},
			.mass = 1
		};
	}
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

// Default height of the viewport in world space with no zoom applied
float cam_default_height = 10;
// Center of camera
vec2_t cam_pos = {0, 0};
// Camera zoom level
float cam_zoom = 1;
bool cam_grabbed = false;

void cam_update(){
	//printf("cam_pos: x %f y %f zoom %f\n", cam_pos.x, cam_pos.y, cam_zoom);
	
	float cam_height = powf(cam_default_height, cam_zoom);
	float aspect_ratio = renderer_screen_size.y / renderer_screen_size.x;
	// Size of the viewport in world space
	vec2_t viewport = { cam_height / aspect_ratio, cam_height };
	//printf("viewport: w %f h %f\n", viewport.x, viewport.y);
	
	float sx = 1 / (viewport.x / 2);
	float sy = 1 / (viewport.y / 2);
	float tx = -cam_pos.x * sx;
	float ty = -cam_pos.y * sy;
	m3_transpose(world_to_normal_mat, (mat3_t){
		sx, 0, tx,
		0, sy, ty,
		0, 0, 1
	});
	
	// Calculate matrix to convert screen coords back to world coords
	sx = viewport.x / renderer_screen_size.x;
	sy = -viewport.y / renderer_screen_size.y;
	tx = cam_pos.x - viewport.x / 2;
	ty = cam_pos.y - viewport.y / 2 + viewport.y;
	
	m3_transpose(screen_to_world_mat, (mat3_t){
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
	renderer_screen_size = (vec2_t){ window_width, window_height };
	SDL_SetVideoMode(window_width, window_height, 24, SDL_OPENGL | SDL_RESIZABLE);
	glViewport(0, 0, window_width, window_height);
	
	float sx = 1.0 / renderer_screen_size.x * 2.0;
	float sy = -1.0 / renderer_screen_size.y * 2.0;
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
	
	//grid_draw();
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
							cam_pos.x -= 1;
							cam_update();
							break;
						case SDLK_RIGHT:
							cam_pos.x += 1;
							cam_update();
							break;
						case SDLK_UP:
							cam_pos.y += 1;
							cam_update();
							break;
						case SDLK_DOWN:
							cam_pos.y -= 1;
							cam_update();
							break;
					}
					break;
				case SDL_MOUSEMOTION:
					cursor_pos.x = e.motion.x;
					cursor_pos.y = e.motion.y;
					
					vec2_t world_cursor = m3_v2_mul(screen_to_world_mat, cursor_pos);
					//printf("world cursor: %f %f\n", world_cursor.x, world_cursor.y);
					
					if (cam_grabbed){
						// Only use the scaling factors from the current screen to world matrix. Since we work
						// with deltas here the offsets are not necessary (in fact would destroy the result).
						cam_pos.x += -screen_to_world_mat[0] * e.motion.xrel;
						cam_pos.y += -screen_to_world_mat[4] * e.motion.yrel;
						cam_update();
					}
					
					break;
				case SDL_MOUSEBUTTONDOWN:
					switch(e.button.button){
						case SDL_BUTTON_LEFT:
							break;
						case SDL_BUTTON_MIDDLE:
							cam_grabbed = true;
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
							cam_grabbed = false;
							break;
						case SDL_BUTTON_RIGHT:
							break;
						case SDL_BUTTON_WHEELUP:
							cam_zoom -= 0.1;
							{
								vec2_t world_cursor = m3_v2_mul(screen_to_world_mat, cursor_pos);
								cam_pos = (vec2_t){
									cam_pos.x + (world_cursor.x - cam_pos.x) * 0.1,
									cam_pos.y + (world_cursor.y - cam_pos.y) * 0.1
								};
							}
							cam_update();
							break;
						case SDL_BUTTON_WHEELDOWN:
							cam_zoom += 0.1;
							{
								vec2_t world_cursor = m3_v2_mul(screen_to_world_mat, cursor_pos);
								cam_pos = (vec2_t){
									cam_pos.x + (world_cursor.x - cam_pos.x) * 0.1,
									cam_pos.y + (world_cursor.y - cam_pos.y) * 0.1
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