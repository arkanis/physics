#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define __USE_XOPEN 1
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>

#include <SDL/SDL.h>

#define GL_VERSION_2_1 1
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>


GLuint prog;
GLuint vertex_shader;
GLuint pixel_shader;
GLint vertex_buffer;
GLint attr_pos, uni_camera, uni_transform, uni_color;

typedef float mat_t[16];
typedef struct { float x, y; } vec_t;
typedef struct { float r, g, b, a; } color_t;


typedef struct {
	vec_t pos, vel, force;
	float mass, ttl;
	color_t color;
} entity_t, *entity_p;

entity_t *entities;
size_t entity_count;


typedef struct {
	vec_t pos;
	float force;
} attractor_t, *attractor_p;

attractor_t *attractors;
size_t attractor_count;


typedef struct {
	vec_t pos, vel;
	color_t color;
	float spawn_rate;
	float min_mass, max_mass;
	float min_ttl, max_ttl;
} emiter_t, *emiter_p;

emiter_t *emiters;
size_t emiter_count;


vec_t cursor = {0, 0};
bool cursor_active = false;
float cursor_min_mass = 2.5, cursor_max_mass = 10;
float cursor_width = 400, cursor_height = 300;
float cursor_spawn_num = 2000;

uint32_t cycle_duration = 1.0 / 60.0 * 1000;
uint16_t win_w = 640, win_h = 480;

mat_t projection = {0};
vec_t cam_pos = {0, 0};
float cam_zoom = 0;
bool cam_moving = 0;


void make_camera(uint16_t window_width, uint16_t window_height){
	// Build projection matrix, details taken from glOrtho documentation
	float l = 0, r = window_width, t = 0, b = window_height, n = -1, f = 1;
	float tx = -(r + l) / (r - l), ty = -(t + b) / (t - b), tz = -(f + n) / (f - n);
	float s = powf(10, cam_zoom);
	glUniformMatrix4fv(uni_camera, 1, GL_TRUE, (float[16]){
		2 / (r-l) * s, 0, 0, tx + cam_pos.x,
		0, 2 / (t-b) * s, 0, ty + cam_pos.y,
		0, 0, -2 / (f - n), tz,
		0, 0, 0, 1
	});
	glGetUniformfv(prog, uni_camera, projection);
	
	//printf("cam zoom: %f, s: %f\n", cam_zoom, powf(10, cam_zoom));
}


GLint load_shader(GLenum shader_type, const char *filename){
	int fd = open(filename, O_RDONLY, 0);
	struct stat file_stat;
	fstat(fd, &file_stat);
	char *code = mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	
	GLint shader = glCreateShader(shader_type);
	glShaderSource(shader, 1, (const char*[]){ code }, (const int[]){ file_stat.st_size });
	
	munmap(code, file_stat.st_size);
	close(fd);
	
	glCompileShader(shader);
	
	GLint result = GL_TRUE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
	if (result == GL_FALSE){
		char buffer[1024];
		glGetShaderInfoLog(shader, 1024, NULL, buffer);
		printf("shader compilation of %s failed:\n%s\n", filename, buffer);
		return 0;
	}
	return shader;
}

void renderer_init(uint16_t window_width, uint16_t window_height, const char *title){
	// Create window
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_SetVideoMode(window_width, window_height, 24, SDL_OPENGL);
	SDL_WM_SetCaption(title, NULL);
	glViewport(0, 0, window_width, window_height);
	
	// Enable alpha blending
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	
	// Load shaders
	GLint vertex_shader = load_shader(GL_VERTEX_SHADER, "small.vs");
	GLint pixel_shader = load_shader(GL_FRAGMENT_SHADER, "small.ps");
	assert(vertex_shader != 0 && pixel_shader != 0);
	
	GLint prog = glCreateProgram();
	glAttachShader(prog, vertex_shader);
	glAttachShader(prog, pixel_shader);
	glLinkProgram(prog);
	
	GLint result = GL_TRUE;
	glGetProgramiv(prog, GL_LINK_STATUS, &result);
	if (result == GL_FALSE){
		char buffer[1024];
		glGetProgramInfoLog(prog, 1024, NULL, buffer);
		printf("vertex and pixel shader linking faild:\n%s\n", buffer);
		exit(1);
	}
	
	// Enum attribs
	GLint active_attrib_count = 0;
	glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &active_attrib_count);
	printf("%d attribs:\n", active_attrib_count);
	for(size_t i = 0; i < active_attrib_count; i++){
		char buffer[512];
		GLint size;
		GLenum type;
		glGetActiveAttrib(prog, i, 512, NULL, &size, &type, buffer);
		printf("- \"%s\": size %d, type %d\n", buffer, size, type);
	}
	
	attr_pos = glGetAttribLocation(prog, "pos");
	assert(attr_pos != -1);
	
	// Enum uniforms
	GLint active_uniform_count = 0;
	glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &active_uniform_count);
	printf("%d uniforms:\n", active_uniform_count);
	for(size_t i = 0; i < active_uniform_count; i++){
		char buffer[512];
		GLint size;
		GLenum type;
		glGetActiveUniform(prog, i, 512, NULL, &size, &type, buffer);
		printf("- \"%s\": size %d, type %d\n", buffer, size, type);
	}
	
	uni_camera = glGetUniformLocation(prog, "camera");
	uni_transform = glGetUniformLocation(prog, "transform");
	uni_color = glGetUniformLocation(prog, "color");
	assert(uni_camera != -1);
	assert(uni_transform != -1);
	assert(uni_color != -1);
	
	glUseProgram(prog);
	
	

	glGenBuffers(1, &vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	
	float data[] = {
		// Rectangle
		1, 1, 0,
		-1, 1, 0,
		-1, -1, 0,
		1, -1, 0
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
	
	
	glEnableVertexAttribArray(attr_pos);
	glClearColor(0.2, 0.2, 0.2, 1.0);
	
	make_camera(window_width, window_height);
}

void renderer_shutdown(){
	glDeleteProgram(prog);
	glDeleteShader(vertex_shader);
	glDeleteShader(pixel_shader);
	
	glDeleteBuffers(1, &vertex_buffer);
}


void build_world(){
	srand(9);
	
	entity_count = 2500;
	entities = realloc(entities, sizeof(entity_t) * entity_count);
	for(size_t i = 0; i < entity_count; i++){
		entities[i].pos.x = (rand() / (float)RAND_MAX) * 500;
		entities[i].pos.y = (rand() / (float)RAND_MAX) * 200;
		entities[i].vel = (vec_t){0, 0};
		entities[i].force = (vec_t){0, 0};
		entities[i].mass = (rand() / (float)RAND_MAX) * 7.5 + 2.5;
		entities[i].color = (color_t){0, 1, 0, 0.5};
		entities[i].ttl = (rand() / (float)RAND_MAX) * 25.0 + 5.0;
	}
	
	attractor_count = 3;
	attractors = realloc(attractors, sizeof(attractor_t) * attractor_count);
	for(size_t i = 0; i < attractor_count; i++){
		attractors[i].pos.x = (win_w / 4) + (rand() / (float)RAND_MAX) * (win_w / 2);
		attractors[i].pos.y = (win_h / 4) + (rand() / (float)RAND_MAX) * (win_h / 2);
		attractors[i].force = (rand() / (float)RAND_MAX) * 25000 + 10000;
	}
	
	emiter_count = 1;
	emiters = realloc(emiters, sizeof(emiter_t) * emiter_count);
	emiters[0] = (emiter_t){
		.pos = (vec_t){10, 10},
		.vel = (vec_t){10, 10},
		.color = (color_t){1, 0, 0, 0.5},
		.spawn_rate = 75,
		.min_mass = 2.5, .max_mass = 10,
		.min_ttl = 10, .max_ttl = 25
	};
}

void simulate(float dt){
	// Emit new entities
	for(size_t i = 0; i < emiter_count; i++){
		emiter_p em = emiters + i;
		size_t to_spawn_count = em->spawn_rate / cycle_duration;
		
		entities = realloc(entities, sizeof(entity_t) * (entity_count + to_spawn_count));
		for(size_t j = entity_count; j < entity_count + to_spawn_count; j++){
			entities[j] = (entity_t){
				.pos = (vec_t){
					.x = em->pos.x + (rand() / (float)RAND_MAX) * 100 - 50,
					.y = em->pos.y + (rand() / (float)RAND_MAX) * 100 - 50
				},
				.vel = em->vel,
				.force = (vec_t){0, 0},
				.mass = em->min_mass + (rand() / (float)RAND_MAX) * (em->max_mass - em->min_mass),
				.color = em->color,
				.ttl = em->min_ttl + (rand() / (float)RAND_MAX) * (em->max_ttl - em->min_ttl)
			};
		}
		
		entity_count += to_spawn_count;
	}
	
	// Apply attractor forces to each entity
	for(size_t i = 0; i < entity_count; i++){
		entities[i].force = (vec_t){0, 0};
		
		for(size_t j = 0; j < attractor_count; j++){
			vec_t to_attractor = (vec_t){
				attractors[j].pos.x - entities[i].pos.x,
				attractors[j].pos.y - entities[i].pos.y
			};
			float length = sqrtf(to_attractor.x*to_attractor.x + to_attractor.y*to_attractor.y);
			if (length > 0) {
				entities[i].force.x += to_attractor.x / length * attractors[j].force / length;
				entities[i].force.y += to_attractor.y / length * attractors[j].force / length;
			}
		}
	}
	
	// Advance entities
	ssize_t life_i = -1; // index of least alive entity
	for(entity_p e = entities; e < entities + entity_count; e++){
		e->ttl -= dt;
		if (e->ttl < 0)
			continue;
		life_i++;
		
		/*
		a = f / m;
		v = v + a*dt;
		s = s + v*dt;
		*/
		vec_t acl;
		acl.x = e->force.x / e->mass;
		acl.y = e->force.y / e->mass;
		e->vel.x += acl.x * dt;
		e->vel.y += acl.y * dt;
		e->pos.x += e->vel.x * dt;
		e->pos.y += e->vel.y * dt;
		
		entities[life_i] = *e;
	}
	
	// Clean up old particles with a time to live < 0
	entity_count = life_i + 1;
	entities = realloc(entities, sizeof(entity_t) * entity_count);
}

void draw(){
	glClear(GL_COLOR_BUFFER_BIT);
	
	float attractor_scale = 250;
	for(size_t i = 0; i < attractor_count; i++){
		glUniform4f(uni_color, 0, 0, 1, 0.25);
		glUniformMatrix4fv(uni_transform, 1, GL_TRUE, (float[]){
			attractors[i].force / attractor_scale, 0, 0, attractors[i].pos.x,
			0, attractors[i].force / attractor_scale, 0, attractors[i].pos.y,
			0, 0, attractors[i].force / attractor_scale, 0,
			0, 0, 0, 1
		});
		glVertexAttribPointer(attr_pos, 3, GL_FLOAT, GL_FALSE, sizeof(float)*3, 0);
		glDrawArrays(GL_QUADS, 0, 4);
	}
	
	float entry_scale = 0.5;
	for(size_t i = 0; i < entity_count; i++){
		float theta = atan2(entities[i].vel.y, entities[i].vel.x);
		float sx = entities[i].mass * entry_scale, sy = entities[i].mass * entry_scale / 4;
		float c = cos(theta), s = sin(theta);
		
		glUniformMatrix4fv(uni_transform, 1, GL_TRUE, (float[]){
			c*sx, -s*sy, 0, entities[i].pos.x,
			s*sx, c*sy, 0, entities[i].pos.y,
			0, 0, 1, 0,
			0, 0, 0, 1
		});
		
		glUniform4f(uni_color, entities[i].color.r, entities[i].color.g, entities[i].color.b, entities[i].color.a);
		glVertexAttribPointer(attr_pos, 3, GL_FLOAT, GL_FALSE, sizeof(float)*3, 0);
		glDrawArrays(GL_QUADS, 0, 4);
	}
	
	float emiter_scale = 20;
	for(size_t i = 0; i < emiter_count; i++){
		glUniform4f(uni_color, 1, 1, 1, 0.5);
		glUniformMatrix4fv(uni_transform, 1, GL_TRUE, (float[]){
			emiter_scale, 0, 0, emiters[i].pos.x,
			0, emiter_scale, 0, emiters[i].pos.y,
			0, 0, emiter_scale, 0,
			0, 0, 0, 1
		});
		glVertexAttribPointer(attr_pos, 3, GL_FLOAT, GL_FALSE, sizeof(float)*3, 0);
		glDrawArrays(GL_QUADS, 0, 4);
	}
	
	/*
	if (marker_active){
		glUniformMatrix4fv(uni_transform, 1, GL_TRUE, (float[]){
			mass, 0, 0, marker.x,
			0, mass, 0, marker.y,
			0, 0, mass, 0,
			0, 0, 0, 1
		});
		glVertexAttribPointer(attr_pos, 3, GL_FLOAT, GL_FALSE, sizeof(float)*3, 0);
		glUniform4f(uni_color, 1, 0, 0, 0.25);
		glDrawArrays(GL_QUADS, 0, 4);
	}
	*/
	
	if (cursor_active){
		glUniformMatrix4fv(uni_transform, 1, GL_TRUE, (float[]){
			5, 0, 0, cursor.x,
			0, 5, 0, cursor.y,
			0, 0, 5, 0,
			0, 0, 0, 1
		});
		glVertexAttribPointer(attr_pos, 3, GL_FLOAT, GL_FALSE, sizeof(float)*3, 0);
		glUniform4f(uni_color, 1, 1, 1, 0.5);
		glDrawArrays(GL_QUADS, 0, 4);
	}
}


int main(int argc, char **argv){
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	renderer_init(win_w, win_h, "GL Basics");
	
	build_world();
	cursor.x = 0; cursor.y = 0;
	
	
	
	SDL_Event e;
	bool quit = false;
	uint32_t ticks = SDL_GetTicks();
	
	while (!quit) {
		while ( SDL_PollEvent(&e) ) {
			//printf("event %d\n", e.type);
			switch(e.type){
				case SDL_QUIT:
					quit = true;
					break;
				case SDL_KEYUP:
					switch(e.key.keysym.sym){
						case SDLK_LEFT:
							//x -= 0.1;
							break;
						case SDLK_RIGHT:
							//x += 0.1;
							break;
						case SDLK_UP:
							//y += 0.1;
							break;
						case SDLK_DOWN:
							//y -= 0.1;
							break;
						case SDLK_r:
							build_world();
							break;
					}
					break;
				case SDL_MOUSEMOTION:
					cursor.x = (float)e.motion.x; // / win_w * 2.0 - 1.0;
					cursor.y = (float)e.motion.y; // / win_h * -2.0 + 1.0;
					
					if (cam_moving){
						cam_pos.x += e.motion.xrel / 100.0;
						cam_pos.y += e.motion.yrel / -100.0;
						make_camera(win_w, win_h);
					}
					
					break;
				case SDL_MOUSEBUTTONDOWN:
					switch(e.button.button){
						case SDL_BUTTON_WHEELUP:
							break;
						case SDL_BUTTON_WHEELDOWN:
							break;
						case SDL_BUTTON_LEFT:
							emiters[0].pos = cursor;
							cursor_active = true;
							//cursor_mass = 2.5;
							break;
						case SDL_BUTTON_MIDDLE:
							cam_moving = true;
							break;
					}
					break;
				case SDL_MOUSEBUTTONUP:
					switch(e.button.button){
						case SDL_BUTTON_WHEELUP:
							//cursor_mass++;
							cam_zoom += 0.1;
							make_camera(win_w, win_h);
							break;
						case SDL_BUTTON_WHEELDOWN:
							//cursor_mass--;
							cam_zoom -= 0.1;
							make_camera(win_w, win_h);
							break;
						case SDL_BUTTON_LEFT:
							emiters[0].vel = (vec_t){
								cursor.x - emiters[0].pos.x,
								cursor.y - emiters[0].pos.y
							};
							cursor_active = false;
							break;
						case SDL_BUTTON_RIGHT:
							entity_count += cursor_spawn_num;
							entities = realloc(entities, sizeof(entity_t) * entity_count);
							for(size_t i = entity_count - cursor_spawn_num - 1; i < entity_count; i++){
								entities[i] = (entity_t){
									.pos = (vec_t){
										.x = cursor.x + (rand() / (float)RAND_MAX) * cursor_width - cursor_width/2,
										.y = cursor.y + (rand() / (float)RAND_MAX) * cursor_height - cursor_height/2
									},
									.vel = (vec_t){0, 0},
									.force = (vec_t){0, 0},
									.mass = cursor_min_mass + (rand() / (float)RAND_MAX) * (cursor_max_mass - cursor_min_mass),
									.color = (color_t){ 0, 1, 0, 0.5 },
									.ttl = (rand() / (float)RAND_MAX) * 25.0 + 5.0
								};
							}
							
							break;
						case SDL_BUTTON_MIDDLE:
							cam_moving = false;
							break;
					}
					break;
			}
		}
		
		draw();
		SDL_GL_SwapBuffers();
		simulate(cycle_duration / 1000.0);
		
		int32_t duration = cycle_duration - (SDL_GetTicks() - ticks);
		if (duration > 0)
			SDL_Delay(duration);
		ticks = SDL_GetTicks();
	}
	
	// Shader cleanup
	renderer_shutdown();
	
	SDL_Quit();
	return 0;
}