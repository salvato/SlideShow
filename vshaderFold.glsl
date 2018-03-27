#version 100

#ifdef GL_ES
precision highp float;
#endif

uniform mat4 mvp_matrix;
uniform vec4 a;
uniform float theta;
uniform float angle;
uniform float xLeft;

float r, R, beta;
vec4 T;
attribute vec4 p;
attribute vec2 a_texcoord;
varying vec2   v_texcoord;


// The built-in varying called "gl_Position" is declared automatically,
// and the shader must write the transformed position to this variable.


void
main() {
    T = vec4(0.0, 0.0, 0.0, 1.0);
    // Compute conical parameters
    R = sqrt((p.x-xLeft)*(p.x-xLeft) + ((p.y+1.0)-a.y)*((p.y+1.0)-a.y));
    r = R*sin(theta);
    beta = asin((p.x-xLeft)/R) / sin(theta);
    // Fold the sheet into the cone
    T.x = r*sin(beta);
    T.y = R+a.y-r*(1.0-cos(beta))*sin(theta);
    T.z = r*(1.0-cos(beta))*cos(theta);

    // Then rotate by angle about the y axis
    T.x = T.x * cos(angle) - T.z * sin(angle);
    T.z = T.x * sin(angle) + T.z * cos(angle);

    // And translate the sheet origin to the left edge
    T.x += xLeft;
    T.y -= 1.0;
    // Calculate vertex position in screen space
    // gl_Position is the special vertex-shader output
    gl_Position = mvp_matrix * T;

    // Pass texture coordinate to fragment shader
    // Value will be automatically interpolated to fragments inside polygon faces
    v_texcoord = a_texcoord;
}
