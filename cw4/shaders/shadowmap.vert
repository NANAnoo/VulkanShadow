#version 450

layout (location = 0) in vec3 position;

layout( set = 0, binding = 0 ) uniform ULightTransform
{
    mat4 M;
} uTransForm;

void main()
{
    gl_Position = uTransForm.M * vec4(position, 1.0);
}
