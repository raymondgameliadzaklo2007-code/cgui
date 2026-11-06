#include "quad_pipeline.hpp"
#include "shader_module.hpp"
#include "../shaders/quad_vert_spv.hpp"
#include "../shaders/quad_frag_spv.hpp"

#include <stdexcept>
#include <cstring>
#include <array>
#include <iostream>

namespace cgui::renderer {

QuadPipeline::QuadPipeline(VkDevice device, VkPhysicalDevice physical_device, VkRenderPass render_pass)
    : device_(device), physical_device_(physical_device) {
    create_unit_quad_buffer();
    create_instance_buffer();
    create_pipeline(render_pass);
}

QuadPipeline::~QuadPipeline() {
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
}

uint32_t QuadPipeline::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
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

void QuadPipeline::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
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

void QuadPipeline::create_unit_quad_buffer() {
    // 2 triangles forming a unit square [0, 1] x [0, 1]
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

void QuadPipeline::create_instance_buffer() {
    VkDeviceSize buffer_size = sizeof(QuadInstanceData) * MAX_QUADS;

    create_buffer(buffer_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  instance_buffer_, instance_memory_);

    // Persistently map the host visible memory for fast per-frame memcpy
    vkMapMemory(device_, instance_memory_, 0, buffer_size, 0, &mapped_instance_data_);
}

void QuadPipeline::create_pipeline(VkRenderPass render_pass) {
    // 1. Shaders from precompiled SPIR-V bytecodes
    ShaderModule vert_shader(device_, std::vector<uint32_t>(QUAD_VERT_SPV, QUAD_VERT_SPV + (QUAD_VERT_SPV_size / sizeof(uint32_t))));
    ShaderModule frag_shader(device_, std::vector<uint32_t>(QUAD_FRAG_SPV, QUAD_FRAG_SPV + (QUAD_FRAG_SPV_size / sizeof(uint32_t))));

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

    // 2. Vertex Bindings:
    // Binding 0: Static per-vertex unit quad
    // Binding 1: Dynamic per-instance element data
    std::array<VkVertexInputBindingDescription, 2> binding_descriptions{};
    binding_descriptions[0].binding = 0;
    binding_descriptions[0].stride = sizeof(UnitVertex);
    binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    binding_descriptions[1].binding = 1;
    binding_descriptions[1].stride = sizeof(QuadInstanceData);
    binding_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    // Attributes:
    // 0: in_unit_pos (vec2)
    // 1: in_rect (vec4)
    // 2: in_color (vec4)
    // 3: in_border_color (vec4)
    // 4: in_params (vec4)
    std::array<VkVertexInputAttributeDescription, 5> attribute_descriptions{};

    attribute_descriptions[0].binding = 0;
    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[0].offset = offsetof(UnitVertex, pos);

    attribute_descriptions[1].binding = 1;
    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[1].offset = offsetof(QuadInstanceData, rect);

    attribute_descriptions[2].binding = 1;
    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[2].offset = offsetof(QuadInstanceData, color);

    attribute_descriptions[3].binding = 1;
    attribute_descriptions[3].location = 3;
    attribute_descriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[3].offset = offsetof(QuadInstanceData, border_color);

    attribute_descriptions[4].binding = 1;
    attribute_descriptions[4].location = 4;
    attribute_descriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[4].offset = offsetof(QuadInstanceData, params);

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = static_cast<uint32_t>(binding_descriptions.size());
    vertex_input_info.pVertexBindingDescriptions = binding_descriptions.data();
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    // 3. Input Assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // 4. Viewport State (Dynamic)
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // 5. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 6. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 7. Color Blending (Alpha Blending)
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

    // 8. Dynamic States
    std::array<VkDynamicState, 2> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    // 9. Pipeline Layout
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create quad pipeline layout");
    }

    // 10. Create Graphics Pipeline
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
        throw std::runtime_error("Failed to create quad graphics pipeline");
    }
}

void QuadPipeline::bind(VkCommandBuffer cmd_buffer, VkExtent2D viewport_extent) const {
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

    VkBuffer vertex_buffers[] = {unit_quad_buffer_, instance_buffer_};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd_buffer, 0, 2, vertex_buffers, offsets);
}

void QuadPipeline::upload_instances(const std::vector<QuadInstanceData>& instances) {
    if (instances.empty() || !mapped_instance_data_) return;

    size_t count = std::min(instances.size(), MAX_QUADS);
    std::memcpy(mapped_instance_data_, instances.data(), count * sizeof(QuadInstanceData));
}

void QuadPipeline::draw(VkCommandBuffer cmd_buffer, uint32_t instance_count) const {
    if (instance_count == 0) return;
    // 6 vertices per unit quad, N instances
    vkCmdDraw(cmd_buffer, 6, instance_count, 0, 0);
}

} // namespace cgui::renderer
