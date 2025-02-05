precision mediump float;
varying vec2 v_uv; 
uniform sampler2D s_texture;

void main(){
    gl_FragColor = texture2D( s_texture, v_uv);
}
