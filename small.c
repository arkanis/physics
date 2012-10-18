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

typedef struct {
	float x, y;
} vec_t;

typedef struct {
	vec_t pos, vel, force;
	float mass;
} entity_t, *entity_p;

typedef struct {
	vec_t pos;
	float force;
} attractor_t, *attractor_p;


#define ENTITY_LIMIT 100
entity_t *entities;
size_t entity_count; // default set by build_world()
attractor_t attractors[3];
vec_t cursor = {0, 0}, marker = {0, 0};
bool cursor_active = false, marker_active = false;
mat_t projection = {0};

uint32_t cycle_duration = 1.0 / 60.0 * 1000;
uint16_t win_w = 640, win_h = 480;
vec_t background_force = { 0, 9.81 };
float cursor_force = 50;
float mass;


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
	
	
	// Build projection matrix, details taken from glOrtho documentation
	float l = 0, r = window_width, t = 0, b = window_height, n = -1, f = 1;
	float tx = -(r + l) / (r - l), ty = -(t + b) / (t - b), tz = -(f + n) / (f - n);
	glUniformMatrix4fv(uni_camera, 1, GL_TRUE, (float[16]){
		2 / (r-l), 0, 0, tx,
		0, 2 / (t-b), 0, ty,
		0, 0, -2 / (f - n), tz,
		0, 0, 0, 1
	});
	glGetUniformfv(prog, uni_camera, projection);
}

void renderer_shutdown(){
	glDeleteProgram(prog);
	glDeleteShader(vertex_shader);
	glDeleteShader(pixel_shader);
	
	glDeleteBuffers(1, &vertex_buffer);
}


void build_world(){
	srand(9);
	entity_count = 10000;
	entities = realloc(entities, sizeof(entity_t) * entity_count);
	for(size_t i = 0; i < entity_count; i++){
		entities[i].pos.x = (rand() / (float)RAND_MAX) * 500;
		entities[i].pos.y = (rand() / (float)RAND_MAX) * 200;
		entities[i].vel = (vec_t){0, 0};
		entities[i].force = (vec_t){0, 0};
		entities[i].mass = (rand() / (float)RAND_MAX) * 7.5 + 2.5;
	}
	
	for(size_t i = 0; i < 3; i++){
		attractors[i].pos.x = (win_w / 4) + (rand() / (float)RAND_MAX) * (win_w / 2);
		attractors[i].pos.y = (win_h / 4) + (rand() / (float)RAND_MAX) * (win_h / 2);
		attractors[i].force = (rand() / (float)RAND_MAX) * 25000 + 10000;
	}
}

void simulate(float dt){
	for(size_t i = 0; i < entity_count; i++){
		entities[i].force = (vec_t){0, 0};
		//entities[i].force = background_force;
		
		if (cursor_active){
			vec_t to_center = (vec_t){
				cursor.x - entities[i].pos.x,
				cursor.y - entities[i].pos.y
			};
			float length = sqrtf(to_center.x*to_center.x + to_center.y*to_center.y);
			entities[i].force.x += to_center.x / length * cursor_force;
			entities[i].force.y += to_center.y / length * cursor_force;
		}
		
		for(size_t j = 0; j < 3; j++){
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
	
	for(size_t i = 0; i < entity_count; i++){
		/*
		a = f / m;
		v = v + a*dt;
		s = s + v*dt;
		*/
		vec_t acl;
		acl.x = entities[i].force.x / entities[i].mass;
		acl.y = entities[i].force.y / entities[i].mass;
		entities[i].vel.x += acl.x * dt;
		entities[i].vel.y += acl.y * dt;
		entities[i].pos.x += entities[i].vel.x * dt;
		entities[i].pos.y += entities[i].vel.y * dt;
	}
	
	
}

void draw(){
	glClear(GL_COLOR_BUFFER_BIT);
	
	float attractor_scale = 250;
	for(size_t i = 0; i < 3; i++){
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
		
		glUniform4f(uni_color, 1, 0, 0, 0.5);
		glVertexAttribPointer(attr_pos, 3, GL_FLOAT, GL_FALSE, sizeof(float)*3, 0);
		glDrawArrays(GL_QUADS, 0, 4);
	}
	
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
					break;
				case SDL_MOUSEBUTTONDOWN:
					switch(e.button.button){
						case SDL_BUTTON_WHEELUP:
							break;
						case SDL_BUTTON_WHEELDOWN:
							break;
						case SDL_BUTTON_LEFT:
							marker = cursor;
							marker_active = true;
							cursor_active = true;
							mass = 2.5;
							break;
					}
					break;
				case SDL_MOUSEBUTTONUP:
					switch(e.button.button){
						case SDL_BUTTON_WHEELUP:
							mass++;
							break;
						case SDL_BUTTON_WHEELDOWN:
							mass--;
							break;
						case SDL_BUTTON_LEFT:
							marker_active = false;
							cursor_active = false;
							entity_count++;
							entities = realloc(entities, sizeof(entity_t) * entity_count);
							entities[entity_count-1].pos = marker;
							entities[entity_count-1].vel = (vec_t){
								cursor.x - marker.x,
								cursor.y - marker.y
							};
							entities[entity_count-1].force = (vec_t){0, 0};
							entities[entity_count-1].mass = mass;
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