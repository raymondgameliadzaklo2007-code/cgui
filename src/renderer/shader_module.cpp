#include "shader_module.hpp"

namespace cgui::renderer {

ShaderModule::ShaderModule(VkDevice device, const std::vector<uint32_t>& spirv_code)
    : device_(device) {
    create_from_code(spirv_code);
}

ShaderModule::ShaderModule(VkDevice device, const std::string& filepath)
    : device_(device) {
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIR-V shader file: " + filepath);
    }

    size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(file_size));
    file.close();

    create_from_code(buffer);
}

void ShaderModule::create_from_code(const std::vector<uint32_t>& spirv_code) {
    if (spirv_code.empty()) {
        throw std::runtime_error("SPIR-V shader code is empty");
    }

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = spirv_code.size() * sizeof(uint32_t);
    create_info.pCode = spirv_code.data();

    if (vkCreateShaderModule(device_, &create_info, nullptr, &module_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shader module");
    }
}

ShaderModule::~ShaderModule() {
    if (device_ && module_) {
        vkDestroyShaderModule(device_, module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }
}

ShaderModule::ShaderModule(ShaderModule&& other) noexcept
    : device_(other.device_), module_(other.module_) {
    other.module_ = VK_NULL_HANDLE;
}

ShaderModule& ShaderModule::operator=(ShaderModule&& other) noexcept {
    if (this != &other) {
        if (device_ && module_) {
            vkDestroyShaderModule(device_, module_, nullptr);
        }
        device_ = other.device_;
        module_ = other.module_;
        other.module_ = VK_NULL_HANDLE;
    }
    return *this;
}

} // namespace cgui::renderer
