#version 420 compatibility

layout(location=0) in vec4 in_position;

out vec4 position;

void main(void)
{
	position = in_position;
}