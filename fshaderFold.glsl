#version 100

#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D texture0;

varying vec2 v_texcoord;
uniform float alpha;


void
main() {
    vec4 texColor0 = texture2D(texture0, v_texcoord);
    gl_FragColor = texColor0;
}

