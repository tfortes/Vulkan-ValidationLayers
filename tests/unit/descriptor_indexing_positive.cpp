/*
 * Copyright (c) 2015-2025 The Khronos Group Inc.
 * Copyright (c) 2015-2025 Valve Corporation
 * Copyright (c) 2015-2025 LunarG, Inc.
 * Copyright (c) 2015-2025 Google, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "../framework/layer_validation_tests.h"
#include "../framework/pipeline_helper.h"
#include "../framework/descriptor_helper.h"

void DescriptorIndexingTest::ComputePipelineShaderTest(const char *shader, std::vector<VkDescriptorSetLayoutBinding> &bindings) {
    AddRequiredExtensions(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_.resize(bindings.size());
    memcpy(pipe.dsl_bindings_.data(), bindings.data(), bindings.size() * sizeof(VkDescriptorSetLayoutBinding));
    pipe.cs_ = VkShaderObj(this, shader, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.CreateComputePipeline();
}

class PositiveDescriptorIndexing : public DescriptorIndexingTest {};

TEST_F(PositiveDescriptorIndexing, BindingPartiallyBound) {
    TEST_DESCRIPTION("Ensure that no validation errors for invalid descriptors if binding is PARTIALLY_BOUND");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredExtensions(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::descriptorBindingPartiallyBound);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    VkDescriptorBindingFlags ds_binding_flags[2] = {};
    VkDescriptorSetLayoutBindingFlagsCreateInfo layout_createinfo_binding_flags = vku::InitStructHelper();
    ds_binding_flags[0] = 0;
    // No Error
    ds_binding_flags[1] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    // Uncomment for Error
    // ds_binding_flags[1] = 0;

    layout_createinfo_binding_flags.bindingCount = 2;
    layout_createinfo_binding_flags.pBindingFlags = ds_binding_flags;

    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                           {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                       },
                                       0, &layout_createinfo_binding_flags, 0);
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    vkt::Buffer buffer(*m_device, 32, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    vkt::Buffer index_buffer(*m_device, sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    descriptor_set.WriteDescriptorBufferInfo(0, buffer, 0, sizeof(uint32_t));
    descriptor_set.UpdateDescriptorSets();

    char const *shader_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform foo_0 { int val; } doit;
        layout(set = 0, binding = 1) uniform foo_1 { int val; } readit;
        void main() {
            if (doit.val == 0)
                gl_Position = vec4(0.0);
            else
                gl_Position = vec4(readit.val);
        }
    )glsl";
    VkShaderObj vs(this, shader_source, VK_SHADER_STAGE_VERTEX_BIT);

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_[0] = vs.GetStageCreateInfo();
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    VkCommandBufferBeginInfo begin_info = vku::InitStructHelper();
    m_command_buffer.Begin(&begin_info);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdBindIndexBuffer(m_command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vk::CmdDrawIndexed(m_command_buffer, 1, 1, 0, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();
    m_default_queue->SubmitAndWait(m_command_buffer);
}

TEST_F(PositiveDescriptorIndexing, UpdateAfterBind) {
    TEST_DESCRIPTION("Test UPDATE_AFTER_BIND does not reset command buffers.");

    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredExtensions(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    AddRequiredExtensions(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::descriptorBindingStorageBufferUpdateAfterBind);
    AddRequiredFeature(vkt::Feature::fragmentStoresAndAtomics);
    AddRequiredFeature(vkt::Feature::synchronization2);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    vkt::Buffer buffer1(*m_device, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    vkt::Buffer buffer2(*m_device, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    vkt::Buffer buffer3(*m_device, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    OneOffDescriptorIndexingSet descriptor_set(
        m_device,
        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT},
         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr, 0}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    descriptor_set.WriteDescriptorBufferInfo(0, buffer1, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.WriteDescriptorBufferInfo(1, buffer3, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();
    descriptor_set.Clear();

    const char fsSource[] = R"glsl(
        #version 450
        layout (set = 0, binding = 0) buffer buf1 {
            float a;
        } ubuf1;
        layout (set = 0, binding = 1) buffer buf2 {
            float a;
        } ubuf2;
        void main() {
           float f = ubuf1.a * ubuf2.a;
        }
    )glsl";
    VkShaderObj fs(this, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {pipe.vs_->GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.pipeline_layout_ = vkt::PipelineLayout(*m_device, {&descriptor_set.layout_});
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    buffer1.Destroy();
    descriptor_set.WriteDescriptorBufferInfo(0, buffer2, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    VkCommandBufferSubmitInfo cb_info = vku::InitStructHelper();
    cb_info.commandBuffer = m_command_buffer;

    VkSubmitInfo2 submit_info = vku::InitStructHelper();
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cb_info;

    vk::QueueSubmit2KHR(m_default_queue->handle(), 1, &submit_info, VK_NULL_HANDLE);
    m_default_queue->Wait();
}

TEST_F(PositiveDescriptorIndexing, PartiallyBoundDescriptors) {
    TEST_DESCRIPTION("Test partially bound descriptors do not reset command buffers.");

    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredExtensions(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    AddRequiredExtensions(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::descriptorBindingStorageBufferUpdateAfterBind);
    AddRequiredFeature(vkt::Feature::descriptorBindingPartiallyBound);
    AddRequiredFeature(vkt::Feature::fragmentStoresAndAtomics);
    AddRequiredFeature(vkt::Feature::synchronization2);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    vkt::Buffer buffer1(*m_device, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    vkt::Buffer buffer3(*m_device, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    OneOffDescriptorIndexingSet descriptor_set(
        m_device,
        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr, 0}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    descriptor_set.WriteDescriptorBufferInfo(0, buffer1, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.WriteDescriptorBufferInfo(1, buffer3, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    const char fsSource[] = R"glsl(
        #version 450
        layout (set = 0, binding = 0) buffer buf1 {
            float a;
        } ubuf1;
        layout (set = 0, binding = 1) buffer buf2 {
            float a;
        } ubuf2;
        void main() {
           float f = ubuf2.a;
        }
    )glsl";
    VkShaderObj fs(this, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {pipe.vs_->GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.pipeline_layout_ = vkt::PipelineLayout(*m_device, {&descriptor_set.layout_});
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    buffer1.Destroy();

    VkCommandBufferSubmitInfo cb_info = vku::InitStructHelper();
    cb_info.commandBuffer = m_command_buffer;

    VkSubmitInfo2 submit_info = vku::InitStructHelper();
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cb_info;

    vk::QueueSubmit2KHR(m_default_queue->handle(), 1, &submit_info, VK_NULL_HANDLE);
    m_default_queue->Wait();
}

TEST_F(PositiveDescriptorIndexing, PipelineShaderBasic) {
    TEST_DESCRIPTION("Test basic usage of GL_EXT_nonuniform_qualifier.");
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    char const *csSource = R"glsl(
        #version 450
        #extension GL_EXT_nonuniform_qualifier : enable
        layout(set=0, binding=0) buffer block { int x; };
        void main() {
            nonuniformEXT int data;
            int table[5];
            data = table[nonuniformEXT(x)];
        }
    )glsl";

    ComputePipelineShaderTest(csSource, bindings);
}

TEST_F(PositiveDescriptorIndexing, PipelineShaderSampler2D) {
    TEST_DESCRIPTION("Indexing into a Sampler2D (combined image sampler).");
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    char const *csSource = R"glsl(
        #version 450
        #extension GL_EXT_nonuniform_qualifier : enable
        layout(set=0, binding=0) buffer block { vec2 x; };
        layout(set=0, binding=1) uniform sampler2D t;
        void main() {
            vec4 vColor4 = texture(t, nonuniformEXT(x));
        }
    )glsl";

    ComputePipelineShaderTest(csSource, bindings);
}

TEST_F(PositiveDescriptorIndexing, PipelineShaderImageBufferArray) {
    TEST_DESCRIPTION("Indexing into a ImageVuffer array (texel buffer).");
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    char const *csSource = R"glsl(
        #version 450
        #extension GL_EXT_nonuniform_qualifier : enable
        layout(set=0, binding=0) buffer block { int x; };
        layout(set=0, binding=1, rgba8ui) uniform uimageBuffer image_buffer_array[];
        void main() {
            vec4 color = vec4(1.0);
            color += imageLoad(image_buffer_array[x], 0);
            // uses a OpCopyObject
            color += imageLoad(image_buffer_array[nonuniformEXT(x)], 0);
        }
    )glsl";

    AddRequiredFeature(vkt::Feature::runtimeDescriptorArray);
    AddRequiredFeature(vkt::Feature::shaderStorageTexelBufferArrayDynamicIndexing);
    AddRequiredFeature(vkt::Feature::shaderStorageTexelBufferArrayNonUniformIndexing);
    ComputePipelineShaderTest(csSource, bindings);
}

TEST_F(PositiveDescriptorIndexing, PipelineShaderMultiArrayIndexing) {
    TEST_DESCRIPTION("Indexing into a nested array.");
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };

    char const *csSource = R"glsl(
        #version 450
        #extension GL_EXT_nonuniform_qualifier : enable
        layout(set = 0, binding = 0) uniform A { uint value; };
        layout(set = 0, binding = 1) uniform B { uint tex_index[1]; };
        layout(set = 0, binding = 2) uniform sampler2D tex[6];
        void main() {
            vec4 color = vec4(1.0);
            color +=  texture(tex[tex_index[value]], vec2(0, 0));
            color +=  texture(tex[tex_index[nonuniformEXT(value)]], vec2(0, 0));
            color +=  texture(tex[nonuniformEXT(tex_index[value])], vec2(0, 0));
        }
    )glsl";

    AddRequiredFeature(vkt::Feature::shaderSampledImageArrayNonUniformIndexing);
    ComputePipelineShaderTest(csSource, bindings);
}

TEST_F(PositiveDescriptorIndexing, DescriptorSetVariableDescriptorCountAllocateInfo) {
    AddRequiredExtensions(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::descriptorBindingVariableDescriptorCount);
    RETURN_IF_SKIP(Init());

    // Don't set VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo ds_layout_ci = vku::InitStructHelper();
    ds_layout_ci.bindingCount = 1;
    ds_layout_ci.pBindings = &binding;
    ds_layout_ci.flags = 0;
    vkt::DescriptorSetLayout ds_layout(*m_device, ds_layout_ci);

    VkDescriptorPoolSize pool_size = {binding.descriptorType, 1};
    VkDescriptorPoolCreateInfo dspci = vku::InitStructHelper();
    dspci.poolSizeCount = 1;
    dspci.pPoolSizes = &pool_size;
    dspci.maxSets = 1;
    vkt::DescriptorPool pool(*m_device, dspci);

    // Ignored because VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT isn't set
    VkDescriptorSetVariableDescriptorCountAllocateInfo count_alloc_info = vku::InitStructHelper();
    count_alloc_info.descriptorSetCount = 1;
    uint32_t variable_count = 1024;
    count_alloc_info.pDescriptorCounts = &variable_count;

    VkDescriptorSetAllocateInfo ds_alloc_info = vku::InitStructHelper(&count_alloc_info);
    ds_alloc_info.descriptorPool = pool;
    ds_alloc_info.descriptorSetCount = 1;
    ds_alloc_info.pSetLayouts = &ds_layout.handle();

    VkDescriptorSet ds = VK_NULL_HANDLE;
    vk::AllocateDescriptorSets(*m_device, &ds_alloc_info, &ds);
}
