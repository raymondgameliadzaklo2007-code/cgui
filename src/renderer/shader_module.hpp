#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <fstream>

namespace cgui::renderer {

class ShaderModule {
public:
    ShaderModule(VkDevice device, const std::vector<uint32_t>& spirv_code);
    ShaderModule(VkDevice device, const std::string& filepath);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&& other) noexcept;
    ShaderModule& operator=(ShaderModule&& other) noexcept;

    [[nodiscard]] VkShaderModule handle() const noexcept { return module_; }

private:
    void create_from_code(const std::vector<uint32_t>& spirv_code);

    VkDevice device_{VK_NULL_HANDLE};
    VkShaderModule module_{VK_NULL_HANDLE};
};

} // namespace cgui::renderer
