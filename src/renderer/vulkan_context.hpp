#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#include <optional>

struct GLFWwindow;

namespace cgui::renderer {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    [[nodiscard]] bool is_complete() const noexcept {
        return graphics_family.has_value() && present_family.has_value();
    }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

class VulkanContext {
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_device_; }
    [[nodiscard]] VkQueue graphics_queue() const noexcept { return graphics_queue_; }
    [[nodiscard]] VkQueue present_queue() const noexcept { return present_queue_; }
    [[nodiscard]] VkSwapchainKHR swapchain() const noexcept { return swapchain_; }
    [[nodiscard]] VkExtent2D swapchain_extent() const noexcept { return swapchain_extent_; }

    void wait_idle() const noexcept;
    void recreate_swapchain();

private:
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    void create_swapchain();
    void create_image_views();
    void cleanup_swapchain();

    QueueFamilyIndices find_queue_families(VkPhysicalDevice device) const;
    SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice device) const;

    GLFWwindow* window_{nullptr};
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphics_queue_{VK_NULL_HANDLE};
    VkQueue present_queue_{VK_NULL_HANDLE};

    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;
    VkFormat swapchain_image_format_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchain_extent_{};

    const std::vector<const char*> device_extensions_ = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

} // namespace cgui::renderer
