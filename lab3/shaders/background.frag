#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 1) uniform sampler2D meadowTexture;
layout(push_constant) uniform Background {
    float viewportAspect;
} background;

void main() {
    vec2 imageSize = vec2(textureSize(meadowTexture, 0));
    float imageAspect = imageSize.x / imageSize.y;
    // Заполняем окно без растяжения фотографии.
    vec2 crop = vec2(min(1.0, background.viewportAspect / imageAspect),
                     min(1.0, imageAspect / background.viewportAspect));
    outColor = texture(meadowTexture, (uv - 0.5) * crop + 0.5);
}
