#pragma once

#include "graphics_internal.hpp"

#include <filesystem>

namespace application {

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& target,
    bool deviceLocal = false);
void destroyBuffer(Buffer& buffer);
bool writeBuffer(const Buffer& buffer, const void* data, size_t size, VkDeviceSize offset = 0);
bool uploadMesh(const void* vertices, size_t vertexBytes, Buffer& vertexBuffer,
    const void* indices, size_t indexBytes, Buffer& indexBuffer);
bool loadTexture(const std::filesystem::path& path, Texture& texture);
void destroyTexture(Texture& texture);

} // namespace application
