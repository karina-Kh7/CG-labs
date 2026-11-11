#include "application.hpp"
#include "gpu_resources.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
namespace application {
    namespace {

        // 1. Простая математика для матриц 4x4
        struct Mat4 {
            float m[16]{};
        };
        // Создаёт единичную матрицу 4x4 — основу для остальных преобразований.
        Mat4 identity() {
            Mat4 result{};
            for (int i = 0; i < 4; ++i)
                result.m[i * 5] = 1.0f;
            return result;
        }
        // Перемножает две матрицы 4x4, чтобы объединять несколько преобразований в одно.
        Mat4 multiply(const Mat4& a, const Mat4& b) {
            Mat4 result{};
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    for (int k = 0; k < 4; ++k)
                        result.m[column * 4 + row] +=
                        a.m[k * 4 + row] * b.m[column * 4 + k];
            return result;
        }
        // Создаёт матрицу переноса объекта на заданное смещение по X, Y и Z.
        Mat4 translate(float x, float y, float z) {
            Mat4 result = identity();
            result.m[12] = x;
            result.m[13] = y;
            result.m[14] = z;
            return result;
        }
        // Создаёт матрицу масштабирования по трём осям.
        Mat4 scale(float x, float y, float z) {
            Mat4 result = identity();
            result.m[0] = x;
            result.m[5] = y;
            result.m[10] = z;
            return result;
        }
        // Создаёт матрицу поворота вокруг оси X.
        Mat4 rotateX(float angle) {
            Mat4 result = identity();
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            result.m[5] = c;
            result.m[6] = s;
            result.m[9] = -s;
            result.m[10] = c;
            return result;
        }
        // Создаёт матрицу поворота вокруг оси Y.
        Mat4 rotateY(float angle) {
            Mat4 result = identity();
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            result.m[0] = c;
            result.m[2] = -s;
            result.m[8] = s;
            result.m[10] = c;
            return result;
        }
        // Создаёт матрицу поворота вокруг оси Z.
        Mat4 rotateZ(float angle) {
            Mat4 result = identity();
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            result.m[0] = c;
            result.m[1] = s;
            result.m[4] = -s;
            result.m[5] = c;
            return result;
        }
        constexpr float pi = 3.14159265358979323846f;
        // Переводит угол из градусов в радианы, так как sin и cos работают с радианами.
        float radians(float degrees) {
            return degrees * pi / 180.0f;
        }
        // Создаёт матрицу перспективной проекции: дальние части объекта выглядят меньше.
        Mat4 perspective(float aspect, float fov) {
            constexpr float nearPlane = 0.1f;
            constexpr float farPlane = 100.0f;
            const float f = 1.0f / std::tan(radians(fov) / 2.0f);
            Mat4 result{};
            result.m[0] = f / aspect;
            result.m[5] = f;
            result.m[10] = farPlane / (nearPlane - farPlane);
            result.m[11] = -1.0f;
            result.m[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }
        // Создаёт ортографическую проекцию: размер объекта не зависит от расстояния до камеры.
        Mat4 orthographic(float aspect) {
            const float height = 4.0f;
            const float width = height * aspect;
            const float nearPlane = 0.1f;
            const float farPlane = 100.0f;
            Mat4 result = identity();
            result.m[0] = 2.0f / width;
            result.m[5] = 2.0f / height;
            result.m[10] = 1.0f / (nearPlane - farPlane);
            result.m[14] = nearPlane / (nearPlane - farPlane);
            return result;
        }

        // 2. Икосаэдр: нормали граней генерируются из векторного произведения.
        struct Vertex { float position[3]; float uv[2]; float normal[3]; };
        // UV задают три точки на картинке, которые соответствуют трём углам каждой грани.
        constexpr float faceUV[3][2] = {{0.58f, 0.01f}, {0.0f, 0.76f}, {1.0f, 0.76f}};
        constexpr float phi = 1.61803398875f;
        constexpr float positions[12][3] = {
            { 0,1,phi },{ 0,-1,phi },{ 0,1,-phi },{ 0,-1,-phi },
            { 1,phi,0 },{ -1,phi,0 },{ 1,-phi,0 },{ -1,-phi,0 },
            { phi,0,1 },{ phi,0,-1 },{ -phi,0,1 },{ -phi,0,-1 }
        };
        constexpr uint16_t faces[60] = {
            0,1,8, 0,10,1, 0,4,5, 0,8,4, 0,5,10,
            1,10,7, 1,7,6, 1,6,8,
            2,3,11, 2,9,3, 2,5,4, 2,11,5, 2,4,9,
            3,6,7, 3,9,6, 3,7,11,
            4,8,9, 5,11,10, 6,9,8, 7,10,11
        };
        // Дублируем вершины по граням, чтобы у каждой плоской грани была своя нормаль.
        const std::array<Vertex, 60> vertices = [] {
            std::array<Vertex, 60> result{};
            for (int f = 0; f < 20; ++f) {
                const auto& a = positions[faces[3 * f]];
                const auto& b = positions[faces[3 * f + 1]];
                const auto& c = positions[faces[3 * f + 2]];
                const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
                const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
                float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                // Всегда направляем нормаль наружу икосаэдра.
                const bool inward = nx * (a[0] + b[0] + c[0]) + ny * (a[1] + b[1] + c[1]) +
                    nz * (a[2] + b[2] + c[2]) < 0;
                if (inward) {
                    nx = -nx; ny = -ny; nz = -nz;
                }
                const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                for (int j = 0;j < 3;++j) {
                    const auto& pos = positions[faces[3 * f + j]];
                    auto& v = result[3 * f + j];
                    std::copy_n(pos, 3, v.position);
                    // У каждой грани свой UV-треугольник. Учитываем порядок обхода вершин.
                    const int uvIndex = inward && j != 0 ? 3 - j : j;
                    std::copy_n(faceUV[uvIndex], 2, v.uv);
                    v.normal[0] = nx * inv; v.normal[1] = ny * inv; v.normal[2] = nz * inv;
                }
            }
            return result;
            }();
        const std::array<uint16_t, 60> indices = [] {
            std::array<uint16_t, 60> result{};
            for (uint16_t i = 0;i < 60;++i) result[i] = i;
            return result;
            }();

        // 3. Vulkan-данные
        // std140/std430: vec4 обеспечивает одинаковое выравнивание в C++ и GLSL.
        struct alignas(16) SceneUniform {
            Mat4 viewProjection;
            float cameraPosition[4];
            float directionalDirection[4];
            float directionalColorIntensity[4];
            float ambientColorIntensity[4];
            int lightCounts[4];
        };
        struct alignas(16) ModelUniform {
            Mat4 model;
            float specular[4]; // xyz - цвет блика, w - степень блеска
            float normalMatrix[16]; // inverse(transpose(model)): масштаб корректно учитывается
        };
        struct alignas(16) PointLight {
            float position[4];
            float colorIntensity[4];
        };
        struct alignas(16) SpotLight {
            float position[4];
            float colorIntensity[4];
            float directionInnerCos[4];
            float outerCosPadding[4];
        };
        static_assert(sizeof(ModelUniform) == 144);
        // Передаём флаг эффекта и время отдельно от свойств модели в UBO.
        struct SamplingPush {
            float time;
            uint32_t enabled;
        };
        static_assert(sizeof(SamplingPush) == 8 && offsetof(SamplingPush, enabled) == 4);
        // Обе фигуры используют одну геометрию. Меняются положение и выбранная текстура.
        constexpr float modelPositions[] = {-1.4f, 1.4f};
        constexpr uint32_t modelCount = std::size(modelPositions);
        constexpr uint32_t animatedModel = 1;

        constexpr const char* textureNames[] = {"Cat with a heart", "Action cat", "Jungkook"};
        constexpr const char* textureFiles[] = {"cat-heart.jpg", "cat-action.jpg", "jungkook.jpg", "meadow.jpg"};
        // Последний JPEG рисуется как фон и не появляется в списках текстур фигур.
        constexpr auto backgroundTexture = std::size(textureFiles) - 1;
        std::array<Texture, std::size(textureFiles)> textures{};
        std::array<VkDescriptorSet, textures.size()> materialSets{};
        std::array<int, modelCount> selectedTextures{0, 1};
        constexpr int modelTextureCount = static_cast<int>(std::size(textureNames));
        static_assert(modelTextureCount == backgroundTexture);
        VkSampler textureSampler = VK_NULL_HANDLE;
        uint32_t modelStride = 0;
        bool animateSampling = true;
        float samplingTime = 0.0f;
        constexpr size_t maxLights = 64; // вместимость CPU/GPU буфера, НЕ размер массива в GLSL
        Buffer vertexBuffer, indexBuffer, sceneUniformBuffer, modelUniformBuffer;
        Buffer pointBuffer, spotBuffer;
        VkDescriptorSetLayout sceneLayout = VK_NULL_HANDLE, modelLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet sceneSet = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipeline backgroundPipeline = VK_NULL_HANDLE;
        std::vector<PointLight> pointLights = {
            { { 2.3f,1.5f,2.0f,0.0f },{ 1.0f,0.35f,0.25f,12.0f } }
        };
        std::vector<SpotLight> spotLights = {
            { { -2.5f,2.5f,2.0f,0.0f },{ 0.30f,0.55f,1.0f,22.0f },
             { 0.5f,-0.7f,-0.5f,16.0f },{ 32.0f,0,0,0 } }
        };
        float cameraPosition[3] = { 0.0f,0.0f,8.0f };
        float cameraRotation[3] = { 0.0f,0.0f,0.0f };
        float cameraFov = 55.0f;
        float directionalDirection[3] = { -0.6f,-1.0f,-0.5f };
        float directionalColor[3] = { 1.0f,1.0f,0.95f };
        float directionalIntensity = 1.6f;
        float ambientColor[3] = { 1.0f,1.0f,1.0f };
        float ambientIntensity = 0.28f;
        float specularColor[3] = { 0.24f,0.24f,0.24f };
        float shininess = 48.0f;

        // 4. Параметры фигуры и анимации
        float position[3] = { 0.0f, 0.0f, 0.0f };
        float rotation[3] = { 18.0f, 25.0f, 0.0f };
        float objectScale[3] = { 1.0f, 1.0f, 1.0f };
        float orbitRadius = 0.42f;
        float animationSpeed = 1.0f;
        float animationPhase = 0.0f;
        bool animationPaused = false;
        bool usePerspective = true;
        double previousTime = -1.0;

        // 6. Загрузка шейдеров
        // Загружает с диска уже скомпилированный SPIR-V shader-файл.
        std::vector<uint32_t> loadShaderFile(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                throw std::runtime_error(std::string("Cannot open shader: ") + path.filename().string());
            const auto fileSize = file.tellg();
            if (fileSize <= 0 || fileSize % 4 != 0)
                throw std::runtime_error("Invalid SPIR-V file");
            std::vector<uint32_t> bytes(static_cast<size_t>(fileSize) / sizeof(uint32_t));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
            if (!file)
                throw std::runtime_error("Cannot read SPIR-V file");
            return bytes;
        }
        // Создаёт Vulkan shader module из загруженного SPIR-V-кода.
        VkShaderModule createShader(const std::filesystem::path& path) {
            auto& context = graphics::internal::context;
            const auto code = loadShaderFile(path);
            const VkShaderModuleCreateInfo shaderInfo{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size() * sizeof(uint32_t),
                .pCode = code.data()
            };
            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(context.device, &shaderInfo, nullptr, &shaderModule) != VK_SUCCESS)
                throw std::runtime_error(std::string("Cannot create shader: ") + path.filename().string());
            return shaderModule;
        }

        // 7. Graphics pipeline
        // Настраивает graphics pipeline: шейдеры, вершины, треугольники, depth test и другие этапы отрисовки.
        bool createPipeline(const std::filesystem::path& shaderDirectory, bool background, VkPipeline& target) {
            auto& context = graphics::internal::context;
            VkShaderModule vertexShader = VK_NULL_HANDLE;
            VkShaderModule fragmentShader = VK_NULL_HANDLE;
            try {
                vertexShader = createShader(shaderDirectory /
                    (background ? "background.vert.spv" : "icosahedron.vert.spv"));
                fragmentShader = createShader(shaderDirectory /
                    (background ? "background.frag.spv" : "icosahedron.frag.spv"));
            }
            catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
                if (vertexShader)
                    vkDestroyShaderModule(context.device, vertexShader, nullptr);
                return false;
            }
            const VkPipelineShaderStageCreateInfo shaderStages[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vertexShader,
                    .pName = "main"
                },
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fragmentShader,
                    .pName = "main"
                }
            };
            // Описываем, как GPU должен читать одну структуру Vertex из vertex buffer.
            const VkVertexInputBindingDescription vertexBinding{
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };
            // Связываем позицию, UV и нормаль с входами вершинного шейдера.
            const VkVertexInputAttributeDescription vertexAttributes[] = {
                { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, position)) },
                { 1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, uv)) },
                { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, normal)) }
            };
            const VkPipelineVertexInputStateCreateInfo vertexInput{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = background ? 0u : 1u,
                .pVertexBindingDescriptions = background ? nullptr : &vertexBinding,
                .vertexAttributeDescriptionCount = background ? 0u : 3u,
                .pVertexAttributeDescriptions = background ? nullptr : vertexAttributes
            };
            // Каждые три индекса образуют отдельный треугольник.
            const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
            };
            const VkPipelineViewportStateCreateInfo viewportState{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1
            };
            // Растеризатор превращает треугольники в набор фрагментов для последующей закраски.
            const VkPipelineRasterizationStateCreateInfo rasterizer{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_NONE,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f
            };
            const VkPipelineMultisampleStateCreateInfo multisampling{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
            };
            // Depth test оставляет видимыми ближайшие к камере поверхности.
            const VkPipelineDepthStencilStateCreateInfo depthStencil{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = background ? VK_FALSE : VK_TRUE,
                .depthWriteEnable = background ? VK_FALSE : VK_TRUE,
                .depthCompareOp = VK_COMPARE_OP_LESS
            };
            const VkPipelineColorBlendAttachmentState blendAttachment{
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            };
            const VkPipelineColorBlendStateCreateInfo colorBlending{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments = &blendAttachment
            };
            const VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };
            const VkPipelineDynamicStateCreateInfo dynamicState{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2,
                .pDynamicStates = dynamicStates
            };
            const VkGraphicsPipelineCreateInfo pipelineInfo{
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = shaderStages,
                .pVertexInputState = &vertexInput,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = pipelineLayout,
                .renderPass = context.render_pass,
                .subpass = 0
            };
            const VkResult result = vkCreateGraphicsPipelines(
                context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &target
            );
            vkDestroyShaderModule(context.device, fragmentShader, nullptr);
            vkDestroyShaderModule(context.device, vertexShader, nullptr);
            return result == VK_SUCCESS;
        }
    } // namespace

    bool initialize(const std::filesystem::path& assetDirectory) {
        auto& c = graphics::internal::context;
        // GPU требует, чтобы начало каждой записи в динамическом UBO было выровнено.
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(c.physical_device, &properties);
        const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
        modelStride = static_cast<uint32_t>((sizeof(ModelUniform) + alignment - 1) / alignment * alignment);

        if (!uploadMesh(vertices.data(), sizeof(vertices), vertexBuffer, indices.data(), sizeof(indices), indexBuffer) ||
            !createBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sceneUniformBuffer) ||
            !createBuffer(modelCount * modelStride, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, modelUniformBuffer) ||
            !createBuffer(maxLights * sizeof(PointLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pointBuffer) ||
            !createBuffer(maxLights * sizeof(SpotLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, spotBuffer))
            return (shutdown(), false);

        const auto textureDirectory = assetDirectory / "textures";
        for (size_t i = 0; i < textures.size(); ++i) {
            if (!loadTexture(textureDirectory / textureFiles[i], textures[i]))
                return (shutdown(), false);
        }
        // Линейная фильтрация смешивает соседние тексели. Координаты за краем дают цвет края.
        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxAnisotropy = 1.0f
        };
        if (vkCreateSampler(c.device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS)
            return (shutdown(), false);

        constexpr auto modelStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        const VkDescriptorSetLayoutBinding sceneBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, modelStages, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
        };
        const VkDescriptorSetLayoutBinding modelBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, modelStages, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
        };
        VkDescriptorSetLayoutCreateInfo desc{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(std::size(sceneBindings)),
            .pBindings = sceneBindings
        };
        if (vkCreateDescriptorSetLayout(c.device, &desc, nullptr, &sceneLayout) != VK_SUCCESS)
            return (shutdown(), false);
        desc.bindingCount = static_cast<uint32_t>(std::size(modelBindings));
        desc.pBindings = modelBindings;
        if (vkCreateDescriptorSetLayout(c.device, &desc, nullptr, &modelLayout) != VK_SUCCESS)
            return (shutdown(), false);

        const VkDescriptorSetLayout layouts[] = {sceneLayout, modelLayout};
        const VkPushConstantRange samplingRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SamplingPush)};
        const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(std::size(layouts)),
            .pSetLayouts = layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &samplingRange
        };
        if (vkCreatePipelineLayout(c.device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
            return (shutdown(), false);

        const auto materialCount = static_cast<uint32_t>(materialSets.size());
        const VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, materialCount},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, materialCount}
        };
        const VkDescriptorPoolCreateInfo pool{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1 + materialCount,
            .poolSizeCount = static_cast<uint32_t>(std::size(sizes)),
            .pPoolSizes = sizes
        };
        if (vkCreateDescriptorPool(c.device, &pool, nullptr, &descriptorPool) != VK_SUCCESS)
            return (shutdown(), false);

        VkDescriptorSetAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &sceneLayout
        };
        if (vkAllocateDescriptorSets(c.device, &allocate, &sceneSet) != VK_SUCCESS)
            return (shutdown(), false);

        const VkDescriptorBufferInfo sceneBuffers[] = {
            {sceneUniformBuffer.handle, 0, sizeof(SceneUniform)},
            {pointBuffer.handle, 0, maxLights * sizeof(PointLight)},
            {spotBuffer.handle, 0, maxLights * sizeof(SpotLight)}
        };
        VkWriteDescriptorSet sceneWrites[std::size(sceneBuffers)]{};
        for (uint32_t i = 0; i < std::size(sceneWrites); ++i) {
            sceneWrites[i] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = sceneSet,
                .dstBinding = i,
                .descriptorCount = 1,
                .descriptorType = sceneBindings[i].descriptorType,
                .pBufferInfo = &sceneBuffers[i]
            };
        }
        vkUpdateDescriptorSets(c.device, static_cast<uint32_t>(std::size(sceneWrites)), sceneWrites, 0, nullptr);

        // Один набор на текстуру. Все наборы используют один и тот же dynamic UBO.
        allocate.pSetLayouts = &modelLayout;
        // Дескриптор описывает размер одной записи. Её начало выбирается при рисовании.
        const VkDescriptorBufferInfo modelInfo{modelUniformBuffer.handle, 0, sizeof(ModelUniform)};
        for (size_t i = 0; i < textures.size(); ++i) {
            if (vkAllocateDescriptorSets(c.device, &allocate, &materialSets[i]) != VK_SUCCESS)
                return (shutdown(), false);
            const VkDescriptorImageInfo imageInfo{
                textureSampler, textures[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            const VkWriteDescriptorSet writes[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = materialSets[i],
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .pBufferInfo = &modelInfo
                },
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = materialSets[i],
                    .dstBinding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &imageInfo
                }
            };
            vkUpdateDescriptorSets(c.device, static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
        }
        const auto shaderDirectory = assetDirectory / "shaders";
        if (!createPipeline(shaderDirectory, false, pipeline) ||
            !createPipeline(shaderDirectory, true, backgroundPipeline))
            return (shutdown(), false);
        return true;
    }

    // 9. Освобождение ресурсов.
    void shutdown() {
        auto& c = graphics::internal::context;
        if (c.graphics_queue)vkQueueWaitIdle(c.graphics_queue);
        if (pipeline)vkDestroyPipeline(c.device, pipeline, nullptr);
        if (backgroundPipeline)vkDestroyPipeline(c.device, backgroundPipeline, nullptr);
        backgroundPipeline = VK_NULL_HANDLE;
        if (pipelineLayout)vkDestroyPipelineLayout(c.device, pipelineLayout, nullptr);
        if (descriptorPool)vkDestroyDescriptorPool(c.device, descriptorPool, nullptr);
        if (sceneLayout)vkDestroyDescriptorSetLayout(c.device, sceneLayout, nullptr);
        if (modelLayout)vkDestroyDescriptorSetLayout(c.device, modelLayout, nullptr);
        pipeline = VK_NULL_HANDLE;pipelineLayout = VK_NULL_HANDLE;
        descriptorPool = VK_NULL_HANDLE;sceneLayout = VK_NULL_HANDLE;modelLayout = VK_NULL_HANDLE;
        if (textureSampler) vkDestroySampler(c.device, textureSampler, nullptr);
        textureSampler = VK_NULL_HANDLE;
        for (auto& texture : textures)
            destroyTexture(texture);
        destroyBuffer(spotBuffer);destroyBuffer(pointBuffer);
        destroyBuffer(modelUniformBuffer);destroyBuffer(sceneUniformBuffer);
        destroyBuffer(indexBuffer);destroyBuffer(vertexBuffer);
    }

    // 10. Управление сценой через ImGui в реальном времени.
    void update(double time) {
        if (previousTime < 0.0)previousTime = time;
        const double delta = std::clamp(time - previousTime, 0.0, 0.1);previousTime = time;
        if (!animationPaused)animationPhase += animationSpeed * static_cast<float>(delta);
        // Пауза вращения не останавливает время текстуры: эффект виден даже на неподвижной грани.
        samplingTime = static_cast<float>(std::fmod(time, 60.0 * pi));
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350.0f, 305.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(330.0f, 260.0f), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::Begin("Lab 3: texture mapping (variant 11)");
        ImGui::PushItemWidth(-148.0f);
        if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Combo("Left texture", &selectedTextures[0], textureNames, modelTextureCount);
            ImGui::Combo("Right texture", &selectedTextures[1], textureNames, modelTextureCount);
            ImGui::Checkbox("Animated sampling (right)", &animateSampling);
        }
        if (ImGui::CollapsingHeader("Camera")) {
            ImGui::DragFloat3("Camera position", cameraPosition, 0.05f);
            ImGui::SliderFloat3("Camera rotation XYZ", cameraRotation, -180.0f, 180.0f);
            ImGui::SliderFloat("Field of view", &cameraFov, 20.0f, 110.0f);
            ImGui::Checkbox("Perspective projection", &usePerspective);
        }
        if (ImGui::CollapsingHeader("Objects and material")) {
            ImGui::DragFloat3("Object position", position, 0.02f);
            ImGui::DragFloat3("Object rotation", rotation, 0.5f);
            ImGui::DragFloat3("Object scale", objectScale, 0.01f, 0.1f, 4.0f);
            ImGui::ColorEdit3("Specular color", specularColor);
            ImGui::SliderFloat("Shininess", &shininess, 1.0f, 256.0f);
            ImGui::Checkbox("Pause animation", &animationPaused);
            ImGui::SliderFloat("Orbit speed", &animationSpeed, 0.0f, 4.0f);
            ImGui::SliderFloat("Orbit radius", &orbitRadius, 0.0f, 2.0f);
        }
        if (ImGui::CollapsingHeader("Directional and ambient light")) {
            ImGui::DragFloat3("Directional vector", directionalDirection, 0.02f);
            ImGui::ColorEdit3("Directional color", directionalColor);
            ImGui::SliderFloat("Directional intensity", &directionalIntensity, 0.0f, 15.0f);
            ImGui::ColorEdit3("Ambient color", ambientColor);
            ImGui::SliderFloat("Ambient intensity", &ambientIntensity, 0.0f, 1.0f);
        }
        if (ImGui::CollapsingHeader("Point lights")) {
            if (pointLights.size() < maxLights && ImGui::Button("Add point light"))
                pointLights.push_back({ { 0,2,2,0 },{ 1,0.8f,0.5f,12 } });
            for (size_t i = 0;i < pointLights.size();) {
                ImGui::PushID(static_cast<int>(i) + 1000);
                ImGui::Separator();ImGui::Text("Point %d", static_cast<int>(i) + 1);
                ImGui::DragFloat3("Position", pointLights[i].position, 0.05f);
                ImGui::ColorEdit3("Color", pointLights[i].colorIntensity);
                ImGui::SliderFloat("Intensity", &pointLights[i].colorIntensity[3], 0.0f, 100.0f);
                bool remove = ImGui::Button("Delete point light");ImGui::PopID();
                if (remove)pointLights.erase(pointLights.begin() + static_cast<std::ptrdiff_t>(i));else ++i;
            }
        }
        if (ImGui::CollapsingHeader("Spot lights")) {
            if (spotLights.size() < maxLights && ImGui::Button("Add spot light"))
                spotLights.push_back({ { 0,3,2,0 },{ 0.6f,0.7f,1,20 },{ 0,-1,-0.5f,16 },{ 28,0,0,0 } });
            for (size_t i = 0;i < spotLights.size();) {
                auto& l = spotLights[i];ImGui::PushID(static_cast<int>(i) + 2000);
                ImGui::Separator();ImGui::Text("Spot %d", static_cast<int>(i) + 1);
                ImGui::DragFloat3("Position", l.position, 0.05f);
                ImGui::DragFloat3("Direction", l.directionInnerCos, 0.02f);
                ImGui::ColorEdit3("Color", l.colorIntensity);
                ImGui::SliderFloat("Intensity", &l.colorIntensity[3], 0.0f, 100.0f);
                // Углы храним в градусах для понятного управления.
                ImGui::SliderFloat("Inner angle", &l.directionInnerCos[3], 1.0f, 85.0f);
                ImGui::SliderFloat("Outer angle", &l.outerCosPadding[0], l.directionInnerCos[3], 89.0f);
                l.outerCosPadding[0] = std::max(l.outerCosPadding[0], l.directionInnerCos[3]);
                bool remove = ImGui::Button("Delete spot light");ImGui::PopID();
                if (remove)spotLights.erase(spotLights.begin() + static_cast<std::ptrdiff_t>(i));else ++i;
            }
        }
        ImGui::PopItemWidth();
        ImGui::End();
    }

    // 11. Отрисовка

    // Формирует матрицы текущего кадра и записывает команды отрисовки икосаэдра.
    void render(const graphics::internal::FrameData& fd) {
        auto& context = graphics::internal::context;
        if (!fd.command_buffer || !fd.framebuffer)
            return;
        // Отношение ширины окна к высоте нужно для правильной формы проекции.
        const float aspect =
            static_cast<float>(context.swapchain_extent.width) /
            static_cast<float>(context.swapchain_extent.height);
        // Доп. №1: выбираем тип проекции.
        const Mat4 projection = usePerspective ? perspective(aspect, cameraFov) : orthographic(aspect);
        // View = обратная матрица перемещения и поворота камеры.
        const Mat4 view = multiply(
            multiply(rotateX(radians(-cameraRotation[0])), rotateY(radians(-cameraRotation[1]))),
            multiply(rotateZ(radians(-cameraRotation[2])),
                translate(-cameraPosition[0], -cameraPosition[1], -cameraPosition[2])));
        // Доп. №3: движение по сложной траектории.
        const Mat4 orbit = translate(
            orbitRadius * std::cos(animationPhase),
            0.35f * std::sin(2.0f * animationPhase),
            orbitRadius * std::sin(animationPhase)
        );
        const Mat4 userTranslation = translate(position[0], position[1], position[2]);
        const Mat4 rotationZ = rotateZ(radians(rotation[2] + animationPhase * 30.0f));
        const Mat4 rotationY = rotateY(radians(rotation[1] + animationPhase * 50.0f));
        const Mat4 rotationX = rotateX(radians(rotation[0]));
        constexpr float baseScale = 0.65f;
        const Mat4 scaling = scale(baseScale * objectScale[0], baseScale * objectScale[1], baseScale * objectScale[2]);
        // Model-матрица объединяет масштабирование, поворот, пользовательский перенос и движение по траектории.
        const Mat4 model = multiply(
            multiply(userTranslation, orbit),
            multiply(
                multiply(rotationZ, rotationY),
                multiply(rotationX, scaling)
            )
        );
        SceneUniform scene{};
        scene.viewProjection = multiply(projection, view);
        for (int k = 0;k < 3;++k) {
            scene.cameraPosition[k] = cameraPosition[k];
            scene.directionalDirection[k] = directionalDirection[k];
            scene.directionalColorIntensity[k] = directionalColor[k];
            scene.ambientColorIntensity[k] = ambientColor[k];
        }
        scene.directionalColorIntensity[3] = directionalIntensity;
        scene.ambientColorIntensity[3] = ambientIntensity;
        scene.lightCounts[0] = static_cast<int>(pointLights.size());
        scene.lightCounts[1] = static_cast<int>(spotLights.size());
        ModelUniform material{};
        std::copy_n(specularColor, 3, material.specular);
        material.specular[3] = shininess;
        // Normal matrix = транспонированная обратная от матрицы модели.
        // Для T*R*S нормали преобразуются R*inverse(S); перенос на них не действует.
        const Mat4 rotationOnly = multiply(rotationZ, multiply(rotationY, rotationX));
        const Mat4 normalMatrix = multiply(rotationOnly, scale(
            1.0f / scaling.m[0], 1.0f / scaling.m[5], 1.0f / scaling.m[10]));
        std::memcpy(material.normalMatrix, normalMatrix.m, sizeof(material.normalMatrix));
        // std430 buffer: передаём только активную часть массива, длина в shader не фиксирована.
        std::vector<SpotLight> gpuSpots = spotLights;
        for (auto& l : gpuSpots) {
            l.directionInnerCos[3] = std::cos(radians(l.directionInnerCos[3]));
            l.outerCosPadding[0] = std::cos(radians(l.outerCosPadding[0]));
        }
        if (!writeBuffer(sceneUniformBuffer, &scene, sizeof(scene)) ||
            (!pointLights.empty() && !writeBuffer(pointBuffer, pointLights.data(), pointLights.size() * sizeof(PointLight))) ||
            (!gpuSpots.empty() && !writeBuffer(spotBuffer, gpuSpots.data(), gpuSpots.size() * sizeof(SpotLight))))
            std::cerr << "Vulkan buffer update failed\n";
        // Записываем свойства обеих фигур в один буфер: левая в начало, правая на один шаг дальше.
        for (uint32_t i = 0; i < modelCount; ++i) {
            material.model = multiply(translate(modelPositions[i], 0.0f, 0.0f), model);
            if (!writeBuffer(modelUniformBuffer, &material, sizeof(material), i * modelStride))
                std::cerr << "Model UBO update failed\n";
        }
        // Дальше идёт обычная запись команд Vulkan.
        // Начинаем заново записывать команды для текущего кадра.
        vkResetCommandBuffer(fd.command_buffer, 0);
        const VkCommandBufferBeginInfo commandBufferBegin{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        vkBeginCommandBuffer(fd.command_buffer, &commandBufferBegin);
        const VkClearValue clearValues[] = {
            { .color = { { 0.0f, 0.0f, 0.0f, 1.0f } } },
            { .depthStencil = { 1.0f, 0 } }
        };
        const VkRenderPassBeginInfo renderPassBegin{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = context.render_pass,
            .framebuffer = fd.framebuffer,
            .renderArea = { { 0, 0 }, context.swapchain_extent },
            .clearValueCount = 2,
            .pClearValues = clearValues
        };
        // Начинаем render pass и выбираем созданный graphics pipeline.
        vkCmdBeginRenderPass(fd.command_buffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

        // Отрицательная высота переворачивает ось Y для Vulkan.
        const VkViewport viewport{
            0.0f,
            static_cast<float>(context.swapchain_extent.height),
            static_cast<float>(context.swapchain_extent.width),
            -static_cast<float>(context.swapchain_extent.height),
            0.0f,
            1.0f
        };
        const VkRect2D scissor{ { 0, 0 }, context.swapchain_extent };
        vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

        // Сначала рисуем фотографию в координатах экрана, без освещения и проверки глубины.
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, backgroundPipeline);
        // Фон читает только текстуру. Смещение 0 нужно для привязки общего layout с динамическим UBO.
        constexpr uint32_t backgroundOffset = 0;
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 1, 1, &materialSets[backgroundTexture], 1, &backgroundOffset);
        vkCmdPushConstants(fd.command_buffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(aspect), &aspect);
        vkCmdDraw(fd.command_buffer, 3, 1, 0, 0);

        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        const VkDeviceSize vertexOffset = 0;
        // Подключаем буферы геометрии: позиции, UV, нормали и индексы треугольников.
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer.handle, &vertexOffset);
        vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);
        // Подключаем descriptor set, через который shader получает MVP-матрицу и цвет.
        vkCmdBindDescriptorSets(
            fd.command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            1,
            &sceneSet,
            0,
            nullptr
        );
        for (uint32_t i = 0; i < modelCount; ++i) {
            // Набор дескрипторов выбирает картинку, смещение в байтах выбирает запись модели.
            const uint32_t modelOffset = i * modelStride;
            const VkDescriptorSet materialSet = materialSets[selectedTextures[i]];
            vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout, 1, 1, &materialSet, 1, &modelOffset);

            // Галочка влияет только на правый икосаэдр. Слева всегда обычное сэмплирование.
            const SamplingPush sampling{samplingTime, i == animatedModel && animateSampling ? 1u : 0u};
            vkCmdPushConstants(fd.command_buffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(sampling), &sampling);
            vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(fd.command_buffer);
        vkEndCommandBuffer(fd.command_buffer);
    }
} // namespace application