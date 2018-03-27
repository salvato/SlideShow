#version 100

#ifdef GL_ES
precision highp float;
#endif

uniform mat4 mvp_matrix;
attribute vec4 p;
attribute vec2 a_texcoord;
varying vec2   v_texcoord;


// The built-in varying called "gl_Position" is declared automatically,
// and the shader must write the transformed position to this variable.


void
main() {
    gl_Position = mvp_matrix * p;
    // Pass texture coordinate to fragment shader
    // Value will be automatically interpolated to fragments inside polygon faces
    v_texcoord = a_texcoord;
}
