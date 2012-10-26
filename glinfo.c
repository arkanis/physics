#include <stdio.h>
#include <SDL/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>


void main(){
	SDL_Init(SDL_INIT_VIDEO);
	
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_SetVideoMode(100, 100, 24, SDL_OPENGL);
	SDL_WM_SetCaption("glinfo", NULL);
	
	printf("OpenGL version information:\n");
	printf("vendor: %s\n", glGetString(GL_VENDOR));
	printf("renderer: %s\n", glGetString(GL_RENDERER));
	printf("version: %s\n", glGetString(GL_VERSION));
	printf("shading language version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	
	GLint value;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
	printf("max texture size: %d\n", value);
	
	printf("extentions:\n");
	GLint ext_count;
	glGetIntegerv(GL_NUM_EXTENSIONS, &ext_count);
	for(size_t i = 0; i < ext_count; i++)
		printf("- %s\n", glGetStringi(GL_EXTENSIONS, i));
	
	SDL_Quit();
}