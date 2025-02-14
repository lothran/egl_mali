#version 100
#extension GL_OES_EGL_image_external : require
 

precision mediump float;
varying vec2 v_uv; 
// uniform sampler2D s_texture2D;
uniform samplerExternalOES s_texture2D;
#define INPUT_RES vec2(1920.0,1080.0)

void main(){
    // gl_FragColor = texture2D( s_texture, v_uv);
    vec3 col = vec3(0.0);
    col += 0.37487566 * texture2D(s_texture2D, v_uv + vec2(-0.75777156,-0.75777156)/INPUT_RES).xyz;
    col += 0.37487566 * texture2D(s_texture2D, v_uv + vec2(0.75777156,-0.75777156)/INPUT_RES).xyz;
    col += 0.37487566 * texture2D(s_texture2D, v_uv + vec2(0.75777156,0.75777156)/INPUT_RES).xyz;
    col += 0.37487566 * texture2D(s_texture2D, v_uv + vec2(-0.75777156,0.75777156)/INPUT_RES).xyz;
    
    col += -0.12487566 * texture2D(s_texture2D, v_uv + vec2(-2.90709914,0.0)/INPUT_RES).xyz;
    col += -0.12487566 * texture2D(s_texture2D, v_uv + vec2(2.90709914,0.0)/INPUT_RES).xyz;
    col += -0.12487566 * texture2D(s_texture2D, v_uv + vec2(0.0,-2.90709914)/INPUT_RES).xyz;
    col += -0.12487566 * texture2D(s_texture2D, v_uv + vec2(0.0,2.90709914)/INPUT_RES).xyz;

    gl_FragColor = vec4(col,1);
  // gl_FragColor=vec4(1,1,1,1);
}
