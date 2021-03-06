#version 400
#extension GL_ARB_geometry_shader4 : enable

layout(triangles) in;

layout(triangle_strip, max_vertices = 3) out;

uniform mat3 NormalMatrix;
//uniform mat4 depthMVP;
uniform mat4 MVP;

in VertexData {
    vec3 normal;
    vec4 worldpos;
    vec2 uv;
} VertexIn[];
 
out VertexData {
    vec3 normal;
    vec4 worldpos;
    vec2 uv;
} VertexOut;
 
void main()
{
	vec4 pos;
	for(int i = 0; i < gl_VerticesIn; i++)
	{
		pos = MVP * gl_in[i].gl_Position;
		//VertexOut.shadow_coords = depthMVP * gl_in[i].gl_Position;
		VertexOut.normal = VertexIn[i].normal;
		VertexOut.uv = VertexIn[i].uv;
		VertexOut.worldpos = VertexIn[i].worldpos / VertexIn[i].worldpos.w;
		gl_Position = pos;

		EmitVertex();
	}

	EndPrimitive();
	
}