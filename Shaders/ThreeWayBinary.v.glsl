//----------------------------------------------------//
//                                                    //
// This is a free rendering engine. The library and   //
// the source code are free. If you use this code as  //
// is or any part of it in any kind of project or     //
// product, please acknowledge the source and its	  //
// author.											  //
//                                                    //
// For manuals, help and instructions, please visit:  //
// http://graphics.cs.aueb.gr/graphics/               //
//                                                    //
//----------------------------------------------------//
#version 330 core
#extension GL_EXT_gpu_shader4 : enable

layout(location = 0) in vec3 position;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 texcoord0;
layout(location = 4) in vec2 texcoord1;
layout(location = 5) in vec3 tangent;

uniform mat4 uniform_model;
uniform mat4 uniform_view_proj;

void main(void)
{
	gl_Position = uniform_model * /*uniform_view_proj * */ vec4(position,1);
}
