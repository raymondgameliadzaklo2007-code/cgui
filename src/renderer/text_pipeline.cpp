#include "text_pipeline.hpp"
#include "shader_module.hpp"
#include "../shaders/text_vert_spv.hpp"
#include "../shaders/text_frag_spv.hpp"

#include <stdexcept>
#include <cstring>
#include <array>
#include <iostream>

namespace cgui::renderer {

struct UnitVertex {
    glm::vec2 pos;
};

TextPipeline::TextPipeline(VkDevice device, VkPhysicalDevice physical_device,
                           VkRenderPass render_pass, const font::FontAtlas& atlas)
    : device_(device), physical_device_(physical_device) {
    create_atlas_texture(atlas);
    create_descriptor_set();
    create_unit_quad_buffer();
    create_instance_buffer();
    create_pipeline(render_pass);
}

TextPipeline::~TextPipeline() {
    if (mapped_instance_data_) {
        vkUnmapMemory(device_, instance_memory_);
        mapped_instance_data_ = nullptr;
    }

    if (instance_buffer_) {
        vkDestroyBuffer(device_, instance_buffer_, nullptr);
        vkFreeMemory(device_, instance_memory_, nullptr);
    }

    if (unit_quad_buffer_) {
        vkDestroyBuffer(device_, unit_quad_buffer_, nullptr);
        vkFreeMemory(device_, unit_quad_memory_, nullptr);
    }

    if (pipeline_) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (layout_) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
    }

    if (desc_pool_) {
        vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
    }
    if (desc_layout_) {
        vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr);
    }

    if (atlas_sampler_) {
        vkDestroySampler(device_, atlas_sampler_, nullptr);
    }
    if (atlas_view_) {
        vkDestroyImageView(device_, atlas_view_, nullptr);
    }
    if (atlas_image_) {
        vkDestroyImage(device_, atlas_image_, nullptr);
        vkFreeMemory(device_, atlas_memory_, nullptr);
    }
}

uint32_t TextPipeline::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type for buffer allocation");
}

void TextPipeline::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer");
    }

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device_, buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device_, &alloc_info, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan buffer memory");
    }

    vkBindBufferMemory(device_, buffer, memory, 0);
}

void TextPipeline::create_atlas_texture(const font::FontAtlas& atlas) {
    VkDeviceSize image_size = atlas.width() * atlas.height() * 4;

    // 1. Create Staging Buffer
    VkBuffer staging_buffer{VK_NULL_HANDLE};
    VkDeviceMemory staging_memory{VK_NULL_HANDLE};
    create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  staging_buffer, staging_memory);

    void* data = nullptr;
    vkMapMemory(device_, staging_memory, 0, image_size, 0, &data);
    std::memcpy(data, atlas.pixel_data().data(), static_cast<size_t>(image_size));
    vkUnmapMemory(device_, staging_memory);

    // 2. Create GPU Image
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = atlas.width();
    image_info.extent.height = atlas.height();
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device_, &image_info, nullptr, &atlas_image_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create MSDF atlas image");
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, atlas_image_, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &alloc_info, nullptr, &atlas_memory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate MSDF atlas image memory");
    }

    vkBindImageMemory(device_, atlas_image_, atlas_memory_, 0);

    // 3. Command Pool & Buffer for Layout Transition & Copy
    VkCommandPool cmd_pool{VK_NULL_HANDLE};
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = 0;
    vkCreateCommandPool(device_, &pool_info, nullptr, &cmd_pool);

    VkCommandBuffer cmd{VK_NULL_HANDLE};
    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = cmd_pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_, &cmd_alloc, &cmd);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    // Transition Undefined -> Transfer Dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = atlas_image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy Buffer to Image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {atlas.width(), atlas.height(), 1};

    vkCmdCopyBufferToImage(cmd, staging_buffer, atlas_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition Transfer Dst -> Shader Read Only
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkQueue queue{VK_NULL_HANDLE};
    vkGetDeviceQueue(device_, 0, 0, &queue);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device_, cmd_pool, 1, &cmd);
    vkDestroyCommandPool(device_, cmd_pool, nullptr);
    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_memory, nullptr);

    // 4. Create Image View
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = atlas_image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_info, nullptr, &atlas_view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create MSDF atlas image view");
    }

    // 5. Create Sampler (Linear filtering for continuous distance field reconstruction)
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device_, &sampler_info, nullptr, &atlas_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create MSDF atlas sampler");
    }
}

void TextPipeline::create_descriptor_set() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &desc_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create text descriptor set layout");
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &desc_pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create text descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &desc_layout_;

    if (vkAllocateDescriptorSets(device_, &alloc_info, &desc_set_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate text descriptor set");
    }

    VkDescriptorImageInfo image_desc{};
    image_desc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_desc.imageView = atlas_view_;
    image_desc.sampler = atlas_sampler_;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = desc_set_;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_desc;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void TextPipeline::create_unit_quad_buffer() {
    const std::array<UnitVertex, 6> unit_quad_vertices = {{
        { {0.0f, 0.0f} },
        { {1.0f, 0.0f} },
        { {1.0f, 1.0f} },
        { {0.0f, 0.0f} },
        { {1.0f, 1.0f} },
        { {0.0f, 1.0f} },
    }};

    VkDeviceSize buffer_size = sizeof(unit_quad_vertices);

    create_buffer(buffer_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  unit_quad_buffer_, unit_quad_memory_);

    void* data = nullptr;
    vkMapMemory(device_, unit_quad_memory_, 0, buffer_size, 0, &data);
    std::memcpy(data, unit_quad_vertices.data(), static_cast<size_t>(buffer_size));
    vkUnmapMemory(device_, unit_quad_memory_);
}

void TextPipeline::create_instance_buffer() {
    VkDeviceSize buffer_size = sizeof(GlyphInstanceData) * MAX_GLYPHS;

    create_buffer(buffer_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  instance_buffer_, instance_memory_);

    vkMapMemory(device_, instance_memory_, 0, buffer_size, 0, &mapped_instance_data_);
}

void TextPipeline::create_pipeline(VkRenderPass render_pass) {
    ShaderModule vert_shader(device_, std::vector<uint32_t>(TEXT_VERT_SPV, TEXT_VERT_SPV + (TEXT_VERT_SPV_size / sizeof(uint32_t))));
    ShaderModule frag_shader(device_, std::vector<uint32_t>(TEXT_FRAG_SPV, TEXT_FRAG_SPV + (TEXT_FRAG_SPV_size / sizeof(uint32_t))));

    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_shader.handle();
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_shader.handle();
    frag_stage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {vert_stage, frag_stage};

    std::array<VkVertexInputBindingDescription, 2> binding_descriptions{};
    binding_descriptions[0].binding = 0;
    binding_descriptions[0].stride = sizeof(UnitVertex);
    binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    binding_descriptions[1].binding = 1;
    binding_descriptions[1].stride = sizeof(GlyphInstanceData);
    binding_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    std::array<VkVertexInputAttributeDescription, 5> attribute_descriptions{};

    // 0: in_unit_pos (vec2)
    attribute_descriptions[0].binding = 0;
    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[0].offset = offsetof(UnitVertex, pos);

    // 1: in_rect (vec4)
    attribute_descriptions[1].binding = 1;
    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[1].offset = offsetof(GlyphInstanceData, rect);

    // 2: in_uv (vec4)
    attribute_descriptions[2].binding = 1;
    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[2].offset = offsetof(GlyphInstanceData, uv);

    // 3: in_color (vec4)
    attribute_descriptions[3].binding = 1;
    attribute_descriptions[3].location = 3;
    attribute_descriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[3].offset = offsetof(GlyphInstanceData, color);

    // 4: in_params (vec4)
    attribute_descriptions[4].binding = 1;
    attribute_descriptions[4].location = 4;
    attribute_descriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[4].offset = offsetof(GlyphInstanceData, params);

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = static_cast<uint32_t>(binding_descriptions.size());
    vertex_input_info.pVertexBindingDescriptions = binding_descriptions.data();
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alpha blending for crisp text antialiasing
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    std::array<VkDynamicState, 2> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &desc_layout_;

    if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create text pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = layout_;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create text graphics pipeline");
    }
}

void TextPipeline::bind(VkCommandBuffer cmd_buffer, VkExtent2D viewport_extent) const {
    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(viewport_extent.width);
    viewport.height = static_cast<float>(viewport_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = viewport_extent;
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &desc_set_, 0, nullptr);

    VkBuffer vertex_buffers[] = {unit_quad_buffer_, instance_buffer_};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd_buffer, 0, 2, vertex_buffers, offsets);
}

void TextPipeline::upload_instances(const std::vector<GlyphInstanceData>& instances) {
    if (instances.empty() || !mapped_instance_data_) return;

    size_t count = std::min(instances.size(), MAX_GLYPHS);
    std::memcpy(mapped_instance_data_, instances.data(), count * sizeof(GlyphInstanceData));
}

void TextPipeline::draw(VkCommandBuffer cmd_buffer, uint32_t instance_count) const {
    if (instance_count == 0) return;
    vkCmdDraw(cmd_buffer, 6, instance_count, 0, 0);
}

} // namespace cgui::renderer
