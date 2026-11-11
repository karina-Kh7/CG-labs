#include "gpu_resources.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace application {
namespace {
using graphics::internal::context;

const VmaAllocationCreateInfo gpuMemory{
    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
};
constexpr VkFormat textureFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkImageSubresourceRange colorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

// Загрузки выполняются при старте. Ждём завершения копирования, затем staging можно освободить.
template <typename RecordCommands>
bool submitUpload(RecordCommands record) {
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context.graphics_queue_index
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(context.device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
        return false;

    const VkCommandBufferAllocateInfo allocate{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    bool ok = vkAllocateCommandBuffers(context.device, &allocate, &cmd) == VK_SUCCESS &&
        vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS;
    if (ok) {
        record(cmd);
        const VkSubmitInfo submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd
        };
        ok = vkEndCommandBuffer(cmd) == VK_SUCCESS &&
            vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
            vkQueueWaitIdle(context.graphics_queue) == VK_SUCCESS;
    }
    vkDestroyCommandPool(context.device, pool, nullptr);
    return ok;
}

void transitionTexture(VkCommandBuffer cmd, VkImage image, bool afterCopy) {
    // Перед копированием разрешаем запись в изображение, после копирования — чтение в шейдере.
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = afterCopy ? VkAccessFlags{VK_ACCESS_TRANSFER_WRITE_BIT} : 0u,
        .dstAccessMask = afterCopy
            ? static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT)
            : static_cast<VkAccessFlags>(VK_ACCESS_TRANSFER_WRITE_BIT),
        .oldLayout = afterCopy ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = afterCopy ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = colorRange
    };
    vkCmdPipelineBarrier(cmd,
        afterCopy ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        afterCopy ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& target, bool deviceLocal) {
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    const VmaAllocationCreateInfo hostMemory{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    const auto& memory = deviceLocal ? gpuMemory : hostMemory;
    return vmaCreateBuffer(context.allocator, &bufferInfo, &memory,
        &target.handle, &target.allocation, nullptr) == VK_SUCCESS;
}

void destroyBuffer(Buffer& buffer) {
    if (buffer.handle)
        vmaDestroyBuffer(context.allocator, buffer.handle, buffer.allocation);
    buffer = {};
}

bool writeBuffer(const Buffer& buffer, const void* data, size_t size, VkDeviceSize offset) {
    void* mapped = nullptr;
    if (vmaMapMemory(context.allocator, buffer.allocation, &mapped) != VK_SUCCESS)
        return false;
    std::memcpy(static_cast<char*>(mapped) + offset, data, size);
    const auto result = vmaFlushAllocation(context.allocator, buffer.allocation, offset, size);
    vmaUnmapMemory(context.allocator, buffer.allocation);
    return result == VK_SUCCESS;
}

bool uploadMesh(const void* vertices, size_t vertexBytes, Buffer& vertexBuffer,
    const void* indices, size_t indexBytes, Buffer& indexBuffer) {
    // Staging-буфер доступен CPU. Через него отправляем вершины и индексы в память GPU.
    Buffer staging;
    const bool ok = createBuffer(vertexBytes + indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging) &&
        writeBuffer(staging, vertices, vertexBytes) &&
        writeBuffer(staging, indices, indexBytes, vertexBytes) &&
        createBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertexBuffer, true) &&
        createBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            indexBuffer, true) &&
        submitUpload([&](VkCommandBuffer cmd) {
            const VkBufferCopy vertexCopy{0, 0, vertexBytes};
            const VkBufferCopy indexCopy{vertexBytes, 0, indexBytes};
            vkCmdCopyBuffer(cmd, staging.handle, vertexBuffer.handle, 1, &vertexCopy);
            vkCmdCopyBuffer(cmd, staging.handle, indexBuffer.handle, 1, &indexCopy);
            const VkMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
        });
    destroyBuffer(staging);
    return ok;
}

bool loadTexture(const std::filesystem::path& path, Texture& texture) {
    // Поток понимает Unicode-путь на Windows. В stb_image передаём сами байты.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (!file || length <= 0 || length > std::numeric_limits<int>::max()) {
        std::cerr << "Cannot open texture: " << path.filename() << '\n';
        return false;
    }
    std::vector<stbi_uc> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length))
        return false;

    // Декодируем JPEG в массив пикселей с четырьмя каналами: R, G, B и A.
    int width = 0, height = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, nullptr, STBI_rgb_alpha),
        stbi_image_free);
    if (!pixels) {
        std::cerr << "Cannot decode texture: " << path.filename() << '\n';
        return false;
    }

    const VkExtent3D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    const auto size = static_cast<size_t>(width) * height * 4;
    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = textureFormat,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    Buffer staging;
    const bool ok = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging) &&
        writeBuffer(staging, pixels.get(), size) &&
        vmaCreateImage(context.allocator, &imageInfo, &gpuMemory, &texture.image,
            &texture.allocation, nullptr) == VK_SUCCESS &&
        submitUpload([&](VkCommandBuffer cmd) {
            transitionTexture(cmd, texture.image, false);
            const VkBufferImageCopy region{
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageExtent = extent
            };
            vkCmdCopyBufferToImage(cmd, staging.handle, texture.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            transitionTexture(cmd, texture.image, true);
        });
    destroyBuffer(staging);
    if (!ok)
        return false;

    // Image view сообщает Vulkan, в каком формате и какой диапазон изображения читать.
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = textureFormat,
        .subresourceRange = colorRange
    };
    return vkCreateImageView(context.device, &viewInfo, nullptr, &texture.view) == VK_SUCCESS;
}

void destroyTexture(Texture& texture) {
    if (texture.view)
        vkDestroyImageView(context.device, texture.view, nullptr);
    if (texture.image)
        vmaDestroyImage(context.allocator, texture.image, texture.allocation);
    texture = {};
}

} // namespace application
