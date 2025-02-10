#version 100
attribute vec2 pos;
varying vec2 v_uv;

void main(){
  gl_Position=vec4(pos,0,1);
  v_uv = pos*0.5+0.5;
}
