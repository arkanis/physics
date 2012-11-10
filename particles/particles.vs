#version 120

uniform mat4 camera;
uniform mat4 transform;
attribute vec3 pos;

void main(){
	gl_Position = (camera * transform) * vec4(pos, 1);
}