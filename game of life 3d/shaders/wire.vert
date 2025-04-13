#version 460

#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

vec3 vertices[24] = {
    { -0.5f, -0.5f, -0.5f },
    {  0.5f, -0.5f, -0.5f },
    { -0.5f, -0.5f, -0.5f },
    { -0.5f,  0.5f, -0.5f },
    { -0.5f, -0.5f, -0.5f },
    { -0.5f, -0.5f,  0.5f },
    {  0.5f, -0.5f, -0.5f },
    {  0.5f,  0.5f, -0.5f },
    {  0.5f, -0.5f, -0.5f },
    {  0.5f, -0.5f,  0.5f },
    {  0.5f,  0.5f, -0.5f },
    {  0.5f,  0.5f,  0.5f },
    {  0.5f, -0.5f,  0.5f },
    {  0.5f,  0.5f,  0.5f },
    { -0.5f, -0.5f,  0.5f },
    {  0.5f, -0.5f,  0.5f },
    { -0.5f,  0.5f, -0.5f },
    {  0.5f,  0.5f, -0.5f },
    { -0.5f,  0.5f, -0.5f },
    { -0.5f,  0.5f,  0.5f },
    { -0.5f, -0.5f,  0.5f },
    { -0.5f,  0.5f,  0.5f },
    { -0.5f,  0.5f,  0.5f },
    {  0.5f,  0.5f,  0.5f }
};

layout(location = 0) out vec4 outColor;

struct Cube {
    vec3 position;
    vec3 size;
    u8vec4 color;
};

layout(buffer_reference, scalar) restrict readonly buffer Cubes {
    Cube cubes[];
};

layout(push_constant, scalar) uniform constants {
    Cubes bda;
    uint offset;
    mat4 transform;
} pcs;

void main() {
    Cube cur = pcs.bda.cubes[gl_VertexIndex / 24];
    
    outColor = vec4(cur.color) / vec4(255.0f);
    gl_Position = pcs.transform * vec4(vertices[gl_VertexIndex % 24] * cur.size + cur.position, 1.0f);
}