#version 330

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

//#extension GL_EXT_gpu_shader4 : enable
//#extension GL_EXT_geometry_shader4 : enable

in vec3 vPosition[3];
in vec3 vNormal[3];
in vec3 vColor[3];
in vec3 vTexCoord[3];
out vec3 fNormal;
out vec3 fPosition;
out vec3 fColor;
out vec3 fTexCoord;
//uniform vec2 WIN_SCALE;
noperspective out vec3 dist;

void main(void)
{
	vec2 WIN_SCALE = vec2(600, 600);

	// taken from 'Single-Pass Wireframe Rendering'
	vec2 p0 = WIN_SCALE * gl_in[0].gl_Position.xy/gl_in[0].gl_Position.w;//gl_PositionIn[0].xy/gl_PositionIn[0].w;
	vec2 p1 = WIN_SCALE * gl_in[1].gl_Position.xy/gl_in[1].gl_Position.w;//gl_PositionIn[1].xy/gl_PositionIn[1].w;
	vec2 p2 = WIN_SCALE * gl_in[2].gl_Position.xy/gl_in[2].gl_Position.w;//gl_PositionIn[2].xy/gl_PositionIn[2].w;
	vec2 v0 = p2-p1;
	vec2 v1 = p2-p0;
	vec2 v2 = p1-p0;
	float area = abs(v1.x*v2.y - v1.y * v2.x);

	dist = vec3(area/length(v0),0,0);
	fPosition = vPosition[0];
	fNormal = vNormal[0];
	fColor = vColor[0];
	fTexCoord = vTexCoord[0];
	gl_Position = gl_in[0].gl_Position;
	EmitVertex();

	dist = vec3(0,area/length(v1),0);
	fPosition = vPosition[1];
	fNormal = vNormal[1];
	fColor = vColor[1];
	fTexCoord = vTexCoord[1];
	gl_Position = gl_in[1].gl_Position;
	EmitVertex();

	dist = vec3(0,0,area/length(v2));
	fPosition = vPosition[2];
	fNormal = vNormal[2];
	fColor = vColor[2];
	fTexCoord = vTexCoord[2];
	gl_Position = gl_in[2].gl_Position;
	EmitVertex();

	EndPrimitive();
}