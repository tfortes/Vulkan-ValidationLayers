/*
 * Copyright (c) 2020-2025 The Khronos Group Inc.
 * Copyright (c) 2020-2025 Valve Corporation
 * Copyright (c) 2020-2025 LunarG, Inc.
 * Copyright (c) 2020-2025 Google, Inc.
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
#include "../layers/gpuav/shaders/gpuav_shaders_constants.h"

class NegativeGpuAVDescriptorClassGeneralBuffer : public GpuAVDescriptorClassGeneralBuffer {
  public:
    void ShaderBufferSizeTest(VkDeviceSize buffer_size, VkDeviceSize binding_offset, VkDeviceSize binding_range,
                              VkDescriptorType descriptor_type, const char *fragment_shader,
                              std::vector<const char *> expected_errors, bool shader_objects = false);
};

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ReachMaxActionsCommandValidationLimit) {
    TEST_DESCRIPTION(
        "Within a single command buffer, add a number of draws that goes above the glsl::kMaxActionsPerCommandBuffer GPU-AV limit. "
        "Expect a warning.");
    AddRequiredExtensions(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::nullDescriptor);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();
    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer storage_texel_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, kHostVisibleMemProps);
    vkt::BufferView storage_buffer_view(*m_device, storage_texel_buffer, VK_FORMAT_R32_SFLOAT);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {3, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});

    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, 4);
    descriptor_set.WriteDescriptorBufferInfo(1, write_buffer, 0, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.WriteDescriptorBufferInfo(2, VK_NULL_HANDLE, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.WriteDescriptorBufferView(3, storage_buffer_view, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    descriptor_set.UpdateDescriptorSets();
    const char vs_source[] = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform ufoo { uint index[]; } u_index;      // index[1]
        layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; } Data;  // data[4]
        layout(set = 0, binding = 2) buffer NullBuffer { uint data[]; } Null;     // VK_NULL_HANDLE
        layout(set = 0, binding = 3, r32f) uniform imageBuffer s_buffer;          // texel_buffer[4]
        void main() {
        vec4 x;
        if (u_index.index[0] == 1) {
        Data.data[0] = Null.data[40];
        }
        else if (u_index.index[0] == 2) {
        imageStore(s_buffer, 0, x);
        }
        else if (u_index.index[0] == 3) {
        x = imageLoad(s_buffer, 0);
        }
        }
        )glsl";

    VkShaderObj vs(this, vs_source, VK_SHADER_STAGE_VERTEX_BIT);
    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_[0] = vs.GetStageCreateInfo();
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    m_errorMonitor->SetDesiredWarning("GPU-AV::Max action per command buffer reached");
    for (uint32_t i = 0; i < 10'000; ++i) {
        vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    }
    m_errorMonitor->VerifyFound();
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    uint32_t *data = (uint32_t *)offset_buffer.Memory().Map();
    *data = 1;
    m_default_queue->SubmitAndWait(m_command_buffer);
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, RobustBuffer) {
    TEST_DESCRIPTION("Check buffer oob validation when per pipeline robustness is enabled");

    AddRequiredExtensions(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);
    RETURN_IF_SKIP(InitGpuAvFramework());

    AddRequiredFeature(vkt::Feature::pipelineRobustness);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();
    vkt::Buffer uniform_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer storage_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, uniform_buffer, 0, 4);
    descriptor_set.WriteDescriptorBufferInfo(1, storage_buffer, 0, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    const char *vertshader = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform foo { uint index[]; } u_index;
        layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; } Data;

        void main() {
            vec4 x;
            if (u_index.index[0] == 0)
                x[0] = u_index.index[1]; // Uniform read OOB
            else
                Data.data[8] = 0xdeadca71; // Storage write OOB
        }
    )glsl";

    VkShaderObj vs(this, vertshader, VK_SHADER_STAGE_VERTEX_BIT);

    VkPipelineRobustnessCreateInfo pipeline_robustness_ci = vku::InitStructHelper();
    pipeline_robustness_ci.uniformBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS;
    pipeline_robustness_ci.storageBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS;

    CreatePipelineHelper robust_pipe(*this, &pipeline_robustness_ci);
    robust_pipe.shader_stages_[0] = vs.GetStageCreateInfo();
    robust_pipe.gp_ci_.layout = pipeline_layout;
    robust_pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, robust_pipe);
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    uint32_t *data = (uint32_t *)uniform_buffer.Memory().Map();
    *data = 0;
    // normally VUID-vkCmdDraw-uniformBuffers-06935
    m_errorMonitor->SetDesiredWarning("size is 4 bytes, 4 bytes were bound, and the highest out of bounds access was at [19] bytes",
                                      3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
    *data = 1;
    // normally VUID-vkCmdDraw-storageBuffers-06936
    m_errorMonitor->SetDesiredWarning(
        "size is 16 bytes, 16 bytes were bound, and the highest out of bounds access was at [35] bytes", 3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, Basic) {
    AddRequiredExtensions(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, 4);
    descriptor_set.WriteDescriptorBufferInfo(1, write_buffer, 0, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    const char vs_source[] = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform ufoo { uint index[]; } u_index;      // index[1]
        layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; } Data;  // data[4]
        void main() {
            Data.data[u_index.index[0]] = 0xdeadca71;
        }
    )glsl";

    VkShaderObj vs(this, vs_source, VK_SHADER_STAGE_VERTEX_BIT);
    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_[0] = vs.GetStageCreateInfo();
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    uint32_t *data = (uint32_t *)offset_buffer.Memory().Map();
    *data = 8;

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936", 3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

void NegativeGpuAVDescriptorClassGeneralBuffer::ShaderBufferSizeTest(VkDeviceSize buffer_size, VkDeviceSize binding_offset,
                                                                     VkDeviceSize binding_range, VkDescriptorType descriptor_type,
                                                                     const char *fragment_shader,
                                                                     std::vector<const char *> expected_errors,
                                                                     bool shader_objects) {
    if (shader_objects) {
        AddRequiredExtensions(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        AddRequiredExtensions(VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
    }
    RETURN_IF_SKIP(InitGpuAvFramework());

    if (shader_objects) {
        AddRequiredFeature(vkt::Feature::dynamicRendering);
        AddRequiredFeature(vkt::Feature::shaderObject);
    }
    RETURN_IF_SKIP(InitState());
    if (shader_objects) {
        InitDynamicRenderTarget();
    } else {
        InitRenderTarget();
    }
    for (const char *error : expected_errors) {
        m_errorMonitor->SetDesiredError(error);
    }

    OneOffDescriptorSet ds(m_device, {{0, descriptor_type, 1, VK_SHADER_STAGE_ALL, nullptr}});

    const vkt::PipelineLayout pipeline_layout(*m_device, {&ds.layout_});

    vkt::Buffer buffer(*m_device, buffer_size,
                       (descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                                                                              : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    ds.WriteDescriptorBufferInfo(0, buffer, binding_offset, binding_range, descriptor_type);
    ds.UpdateDescriptorSets();

    vkt::Shader *vso = nullptr;
    vkt::Shader *fso = nullptr;
    if (shader_objects) {
        vso = new vkt::Shader(*m_device, VK_SHADER_STAGE_VERTEX_BIT,
                              GLSLToSPV(VK_SHADER_STAGE_VERTEX_BIT, kVertexDrawPassthroughGlsl), &ds.layout_.handle());
        fso = new vkt::Shader(*m_device, VK_SHADER_STAGE_FRAGMENT_BIT, GLSLToSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader),
                              &ds.layout_.handle());
    }

    VkShaderObj vs(this, kVertexDrawPassthroughGlsl, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderObj fs(this, fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT);

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.gp_ci_.layout = pipeline_layout;

    VkFormat color_formats = VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfo pipeline_rendering_info = vku::InitStructHelper();
    const auto depth_format = FindSupportedDepthOnlyFormat(Gpu());

    if (shader_objects) {
        pipeline_rendering_info.colorAttachmentCount = 1;
        pipeline_rendering_info.pColorAttachmentFormats = &color_formats;
        pipeline_rendering_info.depthAttachmentFormat = depth_format;
        pipe.gp_ci_.pNext = &pipeline_rendering_info;
    }

    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    if (shader_objects) {
        m_command_buffer.BeginRenderingColor(GetDynamicRenderTarget(), GetRenderTargetArea());
    } else {
        m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    }

    if (shader_objects) {
        const VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT,
                                                VK_SHADER_STAGE_FRAGMENT_BIT};
        const VkShaderEXT shaders[] = {vso->handle(), VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, fso->handle()};
        vk::CmdBindShadersEXT(m_command_buffer, 5u, stages, shaders);
        SetDefaultDynamicStatesAll(m_command_buffer);
    } else {
        vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    }
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &ds.set_, 0, nullptr);

    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    if (shader_objects) {
        m_command_buffer.EndRendering();
    } else {
        m_command_buffer.EndRenderPass();
    }
    m_command_buffer.End();
    m_default_queue->Submit(m_command_buffer);
    vk::DeviceWaitIdle(*m_device);
    m_errorMonitor->VerifyFound();
    DestroyRenderTarget();
    if (shader_objects) {
        delete vso;
        delete fso;
    }
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, UniformBufferTooSmall) {
    TEST_DESCRIPTION("Test that an error is produced when trying to access uniform buffer outside the bound region.");
    char const *fsSource = R"glsl(
        #version 450

        layout(location=0) out vec4 x;
        layout(set=0, binding=0) uniform readonly foo { int x; int y; } bar;
        void main() {
           x = vec4(bar.x, bar.y, 0, 1);
        }
        )glsl";
    std::vector<const char *> expected_errors(gpuav::glsl::kMaxErrorsPerCmd, "VUID-vkCmdDraw-uniformBuffers-06935");
    ShaderBufferSizeTest(4,  // buffer size
                         0,  // binding offset
                         4,  // binding range
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, fsSource, expected_errors);
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, UniformBufferTooSmall2) {
    TEST_DESCRIPTION("Buffer is correct size, but only updating half of it.");
    char const *fsSource = R"glsl(
        #version 450

        layout(location=0) out vec4 x;
        layout(set=0, binding=0) uniform readonly foo {
            int x; // Bound
            int y; // Not bound, illegal to use
        } bar;
        void main() {
           x = vec4(bar.x, bar.y, 0, 1);
        }
        )glsl";
    std::vector<const char *> expected_errors(gpuav::glsl::kMaxErrorsPerCmd, "VUID-vkCmdDraw-uniformBuffers-06935");
    ShaderBufferSizeTest(8,  // buffer size
                         0,  // binding offset
                         4,  // binding range
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, fsSource, expected_errors);
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, StorageBufferTooSmall) {
    TEST_DESCRIPTION("Test that an error is produced when trying to access storage buffer outside the bound region.");

    char const *fsSource = R"glsl(
        #version 450

        layout(location=0) out vec4 x;
        layout(set=0, binding=0) buffer readonly foo { int x; int y; } bar;
        void main(){
           x = vec4(bar.x, bar.y, 0, 1);
        }
        )glsl";

    std::vector<const char *> expected_errors(gpuav::glsl::kMaxErrorsPerCmd, "VUID-vkCmdDraw-storageBuffers-06936");
    ShaderBufferSizeTest(4,  // buffer size
                         0,  // binding offset
                         4,  // binding range
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fsSource, expected_errors);
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, UniformBufferTooSmallArray) {
    TEST_DESCRIPTION(
        "Test that an error is produced when trying to access uniform buffer outside the bound region. Uses array in block "
        "definition.");

    char const *fsSource = R"glsl(
        #version 450

        layout(location=0) out vec4 x;
        layout(set=0, binding=0) uniform readonly foo { int x[17]; } bar;
        void main(){
           int y = 0;
           for (int i = 0; i < 17; i++)
               y += bar.x[i];
           x = vec4(y, 0, 0, 1);
        }
        )glsl";

    std::vector<const char *> expected_errors(gpuav::glsl::kMaxErrorsPerCmd, "VUID-vkCmdDraw-uniformBuffers-06935");
    ShaderBufferSizeTest(64,  // buffer size
                         0,   // binding offset
                         64,  // binding range
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, fsSource, expected_errors);
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, UniformBufferTooSmallNestedStruct) {
    TEST_DESCRIPTION(
        "Test that an error is produced when trying to access uniform buffer outside the bound region. Uses nested struct in block "
        "definition.");

    char const *fsSource = R"glsl(
        #version 450

        struct S {
            int x;
            int y;
        };
        layout(location=0) out vec4 x;
        layout(set=0, binding=0) uniform readonly foo { int a; S b; } bar;
        void main(){
           x = vec4(bar.a, bar.b.x, bar.b.y, 1);
        }
        )glsl";

    std::vector<const char *> expected_errors(gpuav::glsl::kMaxErrorsPerCmd, "VUID-vkCmdDraw-uniformBuffers-06935");
    ShaderBufferSizeTest(8,  // buffer size
                         0,  // binding offset
                         8,  // binding range
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, fsSource, expected_errors);
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ObjectUniformBufferTooSmall) {
    TEST_DESCRIPTION("Test that an error is produced when trying to access uniform buffer outside the bound region.");
    char const *fsSource = R"glsl(
        #version 450

        layout(location=0) out vec4 x;
        layout(set=0, binding=0) uniform readonly foo { int x; int y; } bar;
        void main(){
           x = vec4(bar.x, bar.y, 0, 1);
        }
        )glsl";

    std::vector<const char *> expecetd_errors(gpuav::glsl::kMaxErrorsPerCmd, "VUID-vkCmdDraw-None-08612");
    ShaderBufferSizeTest(4,  // buffer size
                         0,  // binding offset
                         4,  // binding range
                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, fsSource, expecetd_errors, true);
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, GPLWrite) {
    AddRequiredExtensions(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::graphicsPipelineLibrary);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.WriteDescriptorBufferInfo(1, write_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    uint32_t *data = (uint32_t *)offset_buffer.Memory().Map();
    *data = 8;

    static const char vertshader[] = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform Foo { uint index[]; };
        layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; };
        void main() {
            uint index = index[0];
            data[index] = 0xdeadca71;
        }
    )glsl";
    vkt::SimpleGPL pipe(*this, pipeline_layout, vertshader);

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936", 3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, GPLWriteSelectPipeline) {
    TEST_DESCRIPTION("Select shaders within pipeline libraries for instrumentation");
    AddRequiredExtensions(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    AddRequiredExtensions(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::graphicsPipelineLibrary);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    std::vector<VkLayerSettingEXT> layer_settings(2);
    layer_settings[0] = {OBJECT_LAYER_NAME, "gpuav_select_instrumented_shaders", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &kVkTrue};
    std::array<const char *, 2> shader_regexes = {{"bim_bap_boom", ".*_my_pipeline.*"}};
    layer_settings[1] = {OBJECT_LAYER_NAME, "gpuav_shaders_to_instrument", VK_LAYER_SETTING_TYPE_STRING_EXT, size32(shader_regexes),
                         shader_regexes.data()};

    RETURN_IF_SKIP(InitGpuAvFramework(layer_settings));
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.WriteDescriptorBufferInfo(1, write_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    uint32_t *data = (uint32_t *)offset_buffer.Memory().Map();
    *data = 8;

    static const char vertshader[] = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform Foo { uint index[]; };
        layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; };
        void main() {
            uint index = index[0];
            data[index] = 0xdeadca71;
        }
    )glsl";

    // Create VkShaderModule to pass in
    VkShaderObj vs(this, vertshader, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderObj fs(this, kFragmentMinimalGlsl, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDebugUtilsObjectNameInfoEXT name_info = vku::InitStructHelper();
    name_info.objectType = VK_OBJECT_TYPE_PIPELINE;

    CreatePipelineHelper vertex_input_lib(*this);
    vertex_input_lib.InitVertexInputLibInfo();
    vertex_input_lib.CreateGraphicsPipeline(false);

    // For GPU-AV tests this shrinks things so only a single fragment is executed
    VkViewport viewport = {0, 0, 1, 1, 0, 1};
    VkRect2D scissor = {{0, 0}, {1, 1}};

    CreatePipelineHelper pre_raster_lib(*this);
    {
        pre_raster_lib.InitPreRasterLibInfo(&vs.GetStageCreateInfo());
        pre_raster_lib.vp_state_ci_.pViewports = &viewport;
        pre_raster_lib.vp_state_ci_.pScissors = &scissor;
        pre_raster_lib.gp_ci_.layout = pipeline_layout;
        pre_raster_lib.CreateGraphicsPipeline();

        name_info.pObjectName = "oh_my_pipeline";
        name_info.objectHandle = uint64_t(pre_raster_lib.Handle());
        vk::SetDebugUtilsObjectNameEXT(device(), &name_info);
    }

    CreatePipelineHelper frag_shader_lib(*this);
    {
        frag_shader_lib.InitFragmentLibInfo(&fs.GetStageCreateInfo());
        frag_shader_lib.gp_ci_.layout = pipeline_layout;
        frag_shader_lib.CreateGraphicsPipeline(false);
    }

    CreatePipelineHelper frag_out_lib(*this);
    frag_out_lib.InitFragmentOutputLibInfo();
    frag_out_lib.CreateGraphicsPipeline(false);

    VkPipeline libraries[4] = {
        vertex_input_lib,
        pre_raster_lib,
        frag_shader_lib,
        frag_out_lib,
    };
    VkPipelineLibraryCreateInfoKHR link_info = vku::InitStructHelper();
    link_info.libraryCount = size32(libraries);
    link_info.pLibraries = libraries;

    VkGraphicsPipelineCreateInfo exe_pipe_ci = vku::InitStructHelper(&link_info);
    exe_pipe_ci.layout = pipeline_layout;
    m_errorMonitor->SetDesiredInfo("oh_my_pipeline");
    vkt::Pipeline exe_pipe(*m_device, exe_pipe_ci);
    m_errorMonitor->VerifyFound();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, exe_pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936", 3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, GPLWriteSelectShaders) {
    TEST_DESCRIPTION("Select shaders within pipeline libraries for instrumentation");
    AddRequiredExtensions(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    AddRequiredExtensions(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::graphicsPipelineLibrary);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    std::vector<VkLayerSettingEXT> layer_settings(2);
    layer_settings[0] = {OBJECT_LAYER_NAME, "gpuav_select_instrumented_shaders", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &kVkTrue};
    std::array<const char *, 2> shader_regexes = {{"verte[xyz]_foo", ".*gment_.*"}};
    layer_settings[1] = {OBJECT_LAYER_NAME, "gpuav_shaders_to_instrument", VK_LAYER_SETTING_TYPE_STRING_EXT, size32(shader_regexes),
                         shader_regexes.data()};

    RETURN_IF_SKIP(InitGpuAvFramework(layer_settings));
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.WriteDescriptorBufferInfo(1, write_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    uint32_t *data = (uint32_t *)offset_buffer.Memory().Map();
    *data = 8;

    static const char vertshader[] = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform Foo { uint index[]; };
        layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; };
        void main() {
            uint index = index[0];
            data[index] = 0xdeadca71;
        }
    )glsl";

    // Create VkShaderModule to pass in
    VkShaderObj vs(this, vertshader, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderObj fs(this, kFragmentMinimalGlsl, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDebugUtilsObjectNameInfoEXT name_info = vku::InitStructHelper();
    name_info.objectType = VK_OBJECT_TYPE_SHADER_MODULE;

    name_info.pObjectName = "vertex_foo";
    name_info.objectHandle = uint64_t(vs.handle());
    vk::SetDebugUtilsObjectNameEXT(device(), &name_info);

    name_info.pObjectName = "fragment_bar";
    name_info.objectHandle = uint64_t(fs.handle());
    vk::SetDebugUtilsObjectNameEXT(device(), &name_info);

    CreatePipelineHelper vertex_input_lib(*this);
    vertex_input_lib.InitVertexInputLibInfo();
    vertex_input_lib.CreateGraphicsPipeline(false);

    // For GPU-AV tests this shrinks things so only a single fragment is executed
    VkViewport viewport = {0, 0, 1, 1, 0, 1};
    VkRect2D scissor = {{0, 0}, {1, 1}};

    CreatePipelineHelper pre_raster_lib(*this);
    {
        pre_raster_lib.InitPreRasterLibInfo(&vs.GetStageCreateInfo());
        pre_raster_lib.vp_state_ci_.pViewports = &viewport;
        pre_raster_lib.vp_state_ci_.pScissors = &scissor;
        pre_raster_lib.gp_ci_.layout = pipeline_layout;
        pre_raster_lib.CreateGraphicsPipeline();
    }

    CreatePipelineHelper frag_shader_lib(*this);
    {
        frag_shader_lib.InitFragmentLibInfo(&fs.GetStageCreateInfo());
        frag_shader_lib.gp_ci_.layout = pipeline_layout;
        frag_shader_lib.CreateGraphicsPipeline(false);
    }

    CreatePipelineHelper frag_out_lib(*this);
    frag_out_lib.InitFragmentOutputLibInfo();
    frag_out_lib.CreateGraphicsPipeline(false);

    VkPipeline libraries[4] = {
        vertex_input_lib,
        pre_raster_lib,
        frag_shader_lib,
        frag_out_lib,
    };
    VkPipelineLibraryCreateInfoKHR link_info = vku::InitStructHelper();
    link_info.libraryCount = size32(libraries);
    link_info.pLibraries = libraries;

    VkGraphicsPipelineCreateInfo exe_pipe_ci = vku::InitStructHelper(&link_info);
    exe_pipe_ci.layout = pipeline_layout;
    m_errorMonitor->SetDesiredInfo("vertex_foo");
    m_errorMonitor->SetDesiredInfo("fragment_bar");
    vkt::Pipeline exe_pipe(*m_device, exe_pipe_ci);
    m_errorMonitor->VerifyFound();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, exe_pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936", 3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, GPLRead) {
    AddRequiredExtensions(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::graphicsPipelineLibrary);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.WriteDescriptorBufferInfo(1, write_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    static const char vertshader[] = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform Foo { uint index[]; };
        layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; };
        void main() {
            // Uniform buffer stride rounded up to the alignment of a vec4 (16 bytes)
            // so u_index.index[4] accesses bytes 64, 65, 66, and 67
            data[0] = index[4];
        }
    )glsl";
    vkt::SimpleGPL pipe(*this, pipeline_layout, vertshader);

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-uniformBuffers-06935", 3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, GPLReadWriteIndependentSets) {
    AddRequiredExtensions(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::graphicsPipelineLibrary);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    vkt::Buffer uniform_texel_buffer(*m_device, 16, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, kHostVisibleMemProps);
    vkt::BufferView uniform_buffer_view(*m_device, uniform_texel_buffer, VK_FORMAT_R32_SFLOAT);

    OneOffDescriptorSet vertex_set(m_device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
    OneOffDescriptorSet common_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    OneOffDescriptorSet fragment_set(m_device,
                                     {{1, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});

    const vkt::PipelineLayout pipeline_layout_vs(*m_device, {&vertex_set.layout_, &common_set.layout_, nullptr}, {},
                                                 VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);
    const vkt::PipelineLayout pipeline_layout_fs(*m_device, {nullptr, &common_set.layout_, &fragment_set.layout_}, {},
                                                 VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);
    const vkt::PipelineLayout pipeline_layout(*m_device, {&vertex_set.layout_, &common_set.layout_, &fragment_set.layout_}, {},
                                              VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);
    vertex_set.WriteDescriptorBufferInfo(0, write_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    vertex_set.UpdateDescriptorSets();
    common_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, VK_WHOLE_SIZE);
    common_set.UpdateDescriptorSets();
    fragment_set.WriteDescriptorBufferView(1, uniform_buffer_view, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    fragment_set.UpdateDescriptorSets();

    const std::array<VkDescriptorSet, 3> desc_sets = {vertex_set.set_, common_set.set_, fragment_set.set_};

    uint32_t *data = (uint32_t *)offset_buffer.Memory().Map();
    *data = 8;

    static const char vert_shader[] = R"glsl(
        #version 450
        layout(set = 1, binding = 0) uniform ufoo { uint index[]; };        // index[1]
        layout(set = 0, binding = 0) buffer StorageBuffer { uint data[]; }; // data[4]
        const vec2 vertices[3] = vec2[](
            vec2(-1.0, -1.0),
            vec2(1.0, -1.0),
            vec2(0.0, 1.0)
        );
        void main() {
            uint x = index[0];
            data[x] = 0xdeadca71;
            data[0] = index[4];
            gl_Position = vec4(vertices[gl_VertexIndex % 3], 0.0, 1.0);
        }
    )glsl";

    static const char frag_shader[] = R"glsl(
        #version 450
        layout(set = 1, binding = 0) uniform ufoo { uint index[]; };      // index[1]
        layout(set = 2, binding = 1) uniform samplerBuffer u_buffer;      // texel_buffer[4]
        layout(location = 0) out vec4 c_out;
        void main() {
            c_out = vec4(0.0);
        }
    )glsl";

    CreatePipelineHelper vertex_input_lib(*this);
    vertex_input_lib.InitVertexInputLibInfo();
    vertex_input_lib.CreateGraphicsPipeline(false);

    CreatePipelineHelper pre_raster_lib(*this);
    {
        const auto vs_spv = GLSLToSPV(VK_SHADER_STAGE_VERTEX_BIT, vert_shader);
        vkt::GraphicsPipelineLibraryStage vs_stage(vs_spv, VK_SHADER_STAGE_VERTEX_BIT);
        pre_raster_lib.InitPreRasterLibInfo(&vs_stage.stage_ci);
        pre_raster_lib.gp_ci_.layout = pipeline_layout_vs;
        pre_raster_lib.CreateGraphicsPipeline(false);
    }

    CreatePipelineHelper frag_shader_lib(*this);
    {
        const auto fs_spv = GLSLToSPV(VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader);
        vkt::GraphicsPipelineLibraryStage fs_stage(fs_spv, VK_SHADER_STAGE_FRAGMENT_BIT);
        frag_shader_lib.InitFragmentLibInfo(&fs_stage.stage_ci);
        frag_shader_lib.gp_ci_.layout = pipeline_layout_fs;
        frag_shader_lib.CreateGraphicsPipeline(false);
    }

    CreatePipelineHelper frag_out_lib(*this);
    frag_out_lib.InitFragmentOutputLibInfo();
    frag_out_lib.CreateGraphicsPipeline(false);

    VkPipeline libraries[4] = {
        vertex_input_lib,
        pre_raster_lib,
        frag_shader_lib,
        frag_out_lib,
    };
    VkPipelineLibraryCreateInfoKHR link_info = vku::InitStructHelper();
    link_info.libraryCount = size32(libraries);
    link_info.pLibraries = libraries;

    VkGraphicsPipelineCreateInfo exe_pipe_ci = vku::InitStructHelper(&link_info);
    exe_pipe_ci.layout = pipeline_layout;
    vkt::Pipeline exe_pipe(*m_device, exe_pipe_ci);
    ASSERT_TRUE(exe_pipe.initialized());

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, exe_pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0,
                              static_cast<uint32_t>(desc_sets.size()), desc_sets.data(), 0, nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-uniformBuffers-06935", 3);  // read
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, GPLNonInlined) {
    TEST_DESCRIPTION("Make sure GPL works when shader modules are not inlined at pipeline creation time");
    AddRequiredExtensions(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::graphicsPipelineLibrary);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.WriteDescriptorBufferInfo(1, write_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    uint32_t *offset_buffer_ptr = (uint32_t *)offset_buffer.Memory().Map();
    *offset_buffer_ptr = 8;

    static const char vertshader[] = R"glsl(
        #version 450
        layout(set = 0, binding = 0) uniform Uniform { uint offset_buffer[]; };
        layout(set = 0, binding = 1) buffer StorageBuffer { uint write_buffer[]; };
        void main() {
            uint index = offset_buffer[0];
            write_buffer[index] = 0xdeadca71;
        }
    )glsl";
    // Create VkShaderModule to pass in
    VkShaderObj vs(this, vertshader, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderObj fs(this, kFragmentMinimalGlsl, VK_SHADER_STAGE_FRAGMENT_BIT);

    CreatePipelineHelper vertex_input_lib(*this);
    vertex_input_lib.InitVertexInputLibInfo();
    vertex_input_lib.CreateGraphicsPipeline(false);

    // For GPU-AV tests this shrinks things so only a single fragment is executed
    VkViewport viewport = {0, 0, 1, 1, 0, 1};
    VkRect2D scissor = {{0, 0}, {1, 1}};

    CreatePipelineHelper pre_raster_lib(*this);
    {
        pre_raster_lib.InitPreRasterLibInfo(&vs.GetStageCreateInfo());
        pre_raster_lib.vp_state_ci_.pViewports = &viewport;
        pre_raster_lib.vp_state_ci_.pScissors = &scissor;
        pre_raster_lib.gp_ci_.layout = pipeline_layout;
        pre_raster_lib.CreateGraphicsPipeline();
    }

    CreatePipelineHelper frag_shader_lib(*this);
    {
        frag_shader_lib.InitFragmentLibInfo(&fs.GetStageCreateInfo());
        frag_shader_lib.gp_ci_.layout = pipeline_layout;
        frag_shader_lib.CreateGraphicsPipeline(false);
    }

    CreatePipelineHelper frag_out_lib(*this);
    frag_out_lib.InitFragmentOutputLibInfo();
    frag_out_lib.CreateGraphicsPipeline(false);

    VkPipeline libraries[4] = {
        vertex_input_lib,
        pre_raster_lib,
        frag_shader_lib,
        frag_out_lib,
    };
    VkPipelineLibraryCreateInfoKHR link_info = vku::InitStructHelper();
    link_info.libraryCount = size32(libraries);
    link_info.pLibraries = libraries;

    VkGraphicsPipelineCreateInfo exe_pipe_ci = vku::InitStructHelper(&link_info);
    exe_pipe_ci.layout = pipeline_layout;
    vkt::Pipeline exe_pipe(*m_device, exe_pipe_ci);

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, exe_pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936", 3);

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, GPLIndependentSetsUnusedSets) {
    AddRequiredExtensions(VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::graphicsPipelineLibrary);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer offset_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer write_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    vkt::Buffer uniform_texel_buffer(*m_device, 16, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, kHostVisibleMemProps);
    vkt::BufferView uniform_buffer_view(*m_device, uniform_texel_buffer, VK_FORMAT_R32_SFLOAT);
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo());

    OneOffDescriptorSet vertex_set(m_device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
    OneOffDescriptorSet common_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    OneOffDescriptorSet fragment_set(m_device,
                                     {{1, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    // No sampler is ever actually used
    OneOffDescriptorSet bad_set(m_device, {{0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});

    const vkt::PipelineLayout pipeline_layout_vs(*m_device, {&common_set.layout_, &vertex_set.layout_, nullptr}, {},
                                                 VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);
    const vkt::PipelineLayout pipeline_layout_fs(*m_device, {&common_set.layout_, &bad_set.layout_, &fragment_set.layout_}, {},
                                                 VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);
    const vkt::PipelineLayout pipeline_layout(*m_device, {&common_set.layout_, &vertex_set.layout_, &fragment_set.layout_}, {},
                                              VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);
    vertex_set.WriteDescriptorBufferInfo(0, write_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    vertex_set.UpdateDescriptorSets();
    common_set.WriteDescriptorBufferInfo(0, offset_buffer, 0, VK_WHOLE_SIZE);
    common_set.UpdateDescriptorSets();
    fragment_set.WriteDescriptorBufferView(1, uniform_buffer_view, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    fragment_set.UpdateDescriptorSets();
    bad_set.WriteDescriptorImageInfo(0, VK_NULL_HANDLE, sampler, VK_DESCRIPTOR_TYPE_SAMPLER);
    bad_set.UpdateDescriptorSets();

    const char *vert_shader = R"glsl(
        #version 450
        layout(set = 1, binding = 0) buffer StorageBuffer { uint data[]; };
        const vec2 vertices[3] = vec2[](
            vec2(-1.0, -1.0),
            vec2(1.0, -1.0),
            vec2(0.0, 1.0)
        );
        void main() {
            data[24] = 0;
            gl_Position = vec4(vertices[gl_VertexIndex % 3], 0.0, 1.0);
        }
    )glsl";

    const char *frag_shader = R"glsl(
        #version 450
        layout(set = 2, binding = 1) uniform samplerBuffer u_buffer;
        layout(location = 0) out vec4 c_out;
        void main() {
            c_out = vec4(0.0);
        }
    )glsl";

    CreatePipelineHelper vertex_input_lib(*this);
    vertex_input_lib.InitVertexInputLibInfo();
    vertex_input_lib.CreateGraphicsPipeline(false);

    CreatePipelineHelper pre_raster_lib(*this);
    {
        const auto vs_spv = GLSLToSPV(VK_SHADER_STAGE_VERTEX_BIT, vert_shader);
        vkt::GraphicsPipelineLibraryStage vs_stage(vs_spv, VK_SHADER_STAGE_VERTEX_BIT);
        pre_raster_lib.InitPreRasterLibInfo(&vs_stage.stage_ci);
        pre_raster_lib.gp_ci_.layout = pipeline_layout_vs;
        pre_raster_lib.CreateGraphicsPipeline(false);
    }

    CreatePipelineHelper frag_shader_lib(*this);
    {
        const auto fs_spv = GLSLToSPV(VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader);
        vkt::GraphicsPipelineLibraryStage fs_stage(fs_spv, VK_SHADER_STAGE_FRAGMENT_BIT);
        frag_shader_lib.InitFragmentLibInfo(&fs_stage.stage_ci);
        frag_shader_lib.gp_ci_.layout = pipeline_layout_fs;
        frag_shader_lib.CreateGraphicsPipeline(false);
    }

    CreatePipelineHelper frag_out_lib(*this);
    frag_out_lib.InitFragmentOutputLibInfo();
    frag_out_lib.CreateGraphicsPipeline(false);

    VkPipeline libraries[4] = {
        vertex_input_lib,
        pre_raster_lib,
        frag_shader_lib,
        frag_out_lib,
    };
    VkPipelineLibraryCreateInfoKHR link_info = vku::InitStructHelper();
    link_info.libraryCount = size32(libraries);
    link_info.pLibraries = libraries;

    VkGraphicsPipelineCreateInfo exe_pipe_ci = vku::InitStructHelper(&link_info);
    exe_pipe_ci.layout = pipeline_layout;
    vkt::Pipeline exe_pipe(*m_device, exe_pipe_ci);
    ASSERT_TRUE(exe_pipe.initialized());

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, exe_pipe);
    const std::array<VkDescriptorSet, 3> desc_sets = {common_set.set_, vertex_set.set_, fragment_set.set_};
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0,
                              static_cast<uint32_t>(desc_sets.size()), desc_sets.data(), 0, nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936", 3);
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, StorageBuffer) {
    TEST_DESCRIPTION("Make sure OOB is still checked when result is from a BufferDeviceAddress");
    SetTargetApiVersion(VK_API_VERSION_1_2);
    AddRequiredExtensions(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::bufferDeviceAddress);
    AddRequiredFeature(vkt::Feature::shaderInt64);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    char const *cs_source = R"glsl(
        #version 450
        #extension GL_EXT_buffer_reference : enable
        #extension GL_ARB_gpu_shader_int64 : enable

        struct Test {
            float a;
        };

        layout(buffer_reference, std430, buffer_reference_align = 16) buffer TestBuffer {
            Test test;
        };

        Test GetTest(uint64_t ptr) {
            return TestBuffer(ptr).test;
        }

        // 12 bytes large
        layout(set = 0, binding = 0) buffer foo {
            TestBuffer data;
            float x;
        } in_buffer;

        void main() {
            in_buffer.x = GetTest(uint64_t(in_buffer.data)).a;
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_2);
    pipe.CreateComputePipeline();

    vkt::Buffer block_buffer(*m_device, 16, 0, vkt::device_address);
    // too small
    vkt::Buffer in_buffer(*m_device, 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    VkDeviceAddress block_ptr = block_buffer.Address();

    uint8_t *in_buffer_ptr = (uint8_t *)in_buffer.Memory().Map();
    memcpy(in_buffer_ptr, &block_ptr, sizeof(VkDeviceAddress));

    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_.UpdateDescriptorSets();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_.set_, 0, nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, Vector) {
    TEST_DESCRIPTION("index into a vector OOB");

    char const *cs_source = R"glsl(
        #version 450

        // 28 bytes large
        layout(set = 0, binding = 0, std430) buffer foo {
            int a;
            vec3 b; // offset 16
        } in_buffer;

        void main() {
            in_buffer.b.y = 0.0;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 20, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, Matrix) {
    TEST_DESCRIPTION("index into a matrix OOB");

    char const *cs_source = R"glsl(
        #version 450

        // 32 bytes large
        layout(set = 0, binding = 0, std430) buffer foo {
            int a;
            mat3x2 b; // offset 8
        } in_buffer;

        void main() {
            in_buffer.b[2][1] = 0.0;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 30, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, Geometry) {
    TEST_DESCRIPTION("Basic Geometry shader test");
    AddRequiredFeature(vkt::Feature::geometryShader);
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    char const *gsSource = R"glsl(
        #version 450
        layout(triangles) in;
        layout(triangle_strip, max_vertices=3) out;
        layout(set = 0, binding = 0, std430) buffer foo {
            vec3 x;
        } in_buffer;

        void main() {
            in_buffer.x.y = 0.0;
            gl_Position = vec4(1);
            EmitVertex();
        }
    )glsl";

    VkShaderObj vs(this, kVertexPointSizeGlsl, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderObj gs(this, gsSource, VK_SHADER_STAGE_GEOMETRY_BIT);

    CreatePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
    pipe.shader_stages_ = {vs.GetStageCreateInfo(), gs.GetStageCreateInfo(), pipe.fs_->GetStageCreateInfo()};
    pipe.CreateGraphicsPipeline();

    // too small
    vkt::Buffer in_buffer(*m_device, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    pipe.descriptor_set_->WriteDescriptorBufferInfo(0, in_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_->UpdateDescriptorSets();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_->set_, 0, nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936");
    // On Windows Arm, it re-runs the geometry shader 3 times on same primitive
    m_errorMonitor->SetAllowedFailureMsg("VUID-vkCmdDraw-storageBuffers-06936");
    m_errorMonitor->SetAllowedFailureMsg("VUID-vkCmdDraw-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

// TODO - Not being triggered, shader is instrumented, but doesn't seem tessellation shader is executed
TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, DISABLED_TessellationControl) {
    TEST_DESCRIPTION("Basic TessellationControl shader test");
    AddRequiredFeature(vkt::Feature::tessellationShader);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    static const char shader_source[] = R"glsl(
        #version 460
        layout(vertices=3) out;
        layout(set = 0, binding = 0, std430) buffer foo {
            vec3 x;
        } in_buffer;

        void main() {
            in_buffer.x.y = 0.0;
            gl_TessLevelOuter[0] = gl_TessLevelOuter[1] = gl_TessLevelOuter[2] = 1;
            gl_TessLevelInner[0] = 1;
        }
    )glsl";

    VkShaderObj vs(this, kVertexDrawPassthroughGlsl, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderObj tcs(this, shader_source, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    VkShaderObj tes(this, kTessellationEvalMinimalGlsl, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

    VkPipelineTessellationStateCreateInfo tess_ci = vku::InitStructHelper();
    tess_ci.patchControlPoints = 4u;

    CreatePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
    pipe.ia_ci_.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    pipe.tess_ci_ = tess_ci;
    pipe.shader_stages_ = {vs.GetStageCreateInfo(), tcs.GetStageCreateInfo(), tes.GetStageCreateInfo(),
                           pipe.fs_->GetStageCreateInfo()};
    pipe.CreateGraphicsPipeline();

    // too small
    vkt::Buffer in_buffer(*m_device, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    pipe.descriptor_set_->WriteDescriptorBufferInfo(0, in_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_->UpdateDescriptorSets();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_->set_, 0, nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

// TODO - Not being triggered, shader is instrumented, but doesn't seem tessellation shader is executed
TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, DISABLED_TessellationEvaluation) {
    TEST_DESCRIPTION("Basic TessellationEvaluation shader test");
    AddRequiredFeature(vkt::Feature::tessellationShader);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    static const char shader_source[] = R"glsl(
        #version 460
        layout(triangles, equal_spacing, cw) in;
        layout(set = 0, binding = 0, std430) buffer foo {
            vec3 x;
        } in_buffer;
        void main() {
            in_buffer.x.y = 0.0;
            gl_Position = vec4(1);
        }
    )glsl";

    VkShaderObj vs(this, kVertexDrawPassthroughGlsl, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderObj tcs(this, kTessellationControlMinimalGlsl, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    VkShaderObj tes(this, shader_source, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

    VkPipelineTessellationStateCreateInfo tess_ci = vku::InitStructHelper();
    tess_ci.patchControlPoints = 4u;

    CreatePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
    pipe.ia_ci_.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    pipe.tess_ci_ = tess_ci;
    pipe.shader_stages_ = {vs.GetStageCreateInfo(), tcs.GetStageCreateInfo(), tes.GetStageCreateInfo(),
                           pipe.fs_->GetStageCreateInfo()};
    pipe.CreateGraphicsPipeline();

    // too small
    vkt::Buffer in_buffer(*m_device, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    pipe.descriptor_set_->WriteDescriptorBufferInfo(0, in_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_->UpdateDescriptorSets();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_->set_, 0, nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, VertexFragmentMultiEntrypoint) {
    AddRequiredFeature(vkt::Feature::vertexPipelineStoresAndAtomics);
    AddRequiredFeature(vkt::Feature::fragmentStoresAndAtomics);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer uniform_buffer(*m_device, 4, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kHostVisibleMemProps);
    vkt::Buffer storage_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    OneOffDescriptorSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                  {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, uniform_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.WriteDescriptorBufferInfo(1, storage_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    // layout(set = 0, binding = 0) uniform ufoo { uint index[]; };
    // layout(set = 0, binding = 1) buffer StorageBuffer { uint data[]; };
    // layout(location = 0) out vec4 c_out;
    // void vert_main() {
    //     data[0] = index[4];
    //     gl_Position = vec4(vertices[gl_VertexIndex % 3], 0.0, 1.0);
    // }
    // void frag_main() {
    //     data[4] = index[0];
    // }
    const char *shader_source = R"(
               OpCapability Shader
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %frag_main "frag_main" %c_out
               OpEntryPoint Vertex %vert_main "vert_main" %2 %gl_VertexIndex
               OpExecutionMode %frag_main OriginUpperLeft
               OpDecorate %_runtimearr_uint ArrayStride 4
               OpMemberDecorate %_struct_7 0 Offset 0
               OpDecorate %_struct_7 BufferBlock
               OpDecorate %8 DescriptorSet 0
               OpDecorate %8 Binding 1
               OpDecorate %_arr_uint_uint_5 ArrayStride 16
               OpMemberDecorate %_struct_10 0 Offset 0
               OpDecorate %_struct_10 Block
               OpDecorate %11 DescriptorSet 0
               OpDecorate %11 Binding 0
               OpDecorate %c_out Location 0
               OpMemberDecorate %_struct_12 0 BuiltIn Position
               OpMemberDecorate %_struct_12 1 BuiltIn PointSize
               OpMemberDecorate %_struct_12 2 BuiltIn ClipDistance
               OpMemberDecorate %_struct_12 3 BuiltIn CullDistance
               OpDecorate %_struct_12 Block
               OpDecorate %gl_VertexIndex BuiltIn VertexIndex
       %void = OpTypeVoid
         %14 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
%_runtimearr_uint = OpTypeRuntimeArray %uint
  %_struct_7 = OpTypeStruct %_runtimearr_uint
%_ptr_Uniform__struct_7 = OpTypePointer Uniform %_struct_7
          %8 = OpVariable %_ptr_Uniform__struct_7 Uniform
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
     %uint_5 = OpConstant %uint 5
%_arr_uint_uint_5 = OpTypeArray %uint %uint_5
 %_struct_10 = OpTypeStruct %_arr_uint_uint_5
%_ptr_Uniform__struct_10 = OpTypePointer Uniform %_struct_10
         %11 = OpVariable %_ptr_Uniform__struct_10 Uniform
      %int_4 = OpConstant %int 4
%_ptr_Uniform_uint = OpTypePointer Uniform %uint
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
     %uint_1 = OpConstant %uint 1
%_arr_float_uint_1 = OpTypeArray %float %uint_1
 %_struct_12 = OpTypeStruct %v4float %float %_arr_float_uint_1 %_arr_float_uint_1
%_ptr_Output__struct_12 = OpTypePointer Output %_struct_12
          %2 = OpVariable %_ptr_Output__struct_12 Output
    %v2float = OpTypeVector %float 2
     %uint_3 = OpConstant %uint 3
%_arr_v2float_uint_3 = OpTypeArray %v2float %uint_3
   %float_n1 = OpConstant %float -1
         %32 = OpConstantComposite %v2float %float_n1 %float_n1
    %float_1 = OpConstant %float 1
         %34 = OpConstantComposite %v2float %float_1 %float_n1
    %float_0 = OpConstant %float 0
         %36 = OpConstantComposite %v2float %float_0 %float_1
         %37 = OpConstantComposite %_arr_v2float_uint_3 %32 %34 %36
%_ptr_Input_int = OpTypePointer Input %int
%gl_VertexIndex = OpVariable %_ptr_Input_int Input
      %int_3 = OpConstant %int 3
%_ptr_Function__arr_v2float_uint_3 = OpTypePointer Function %_arr_v2float_uint_3
%_ptr_Function_v2float = OpTypePointer Function %v2float
%_ptr_Output_v4float = OpTypePointer Output %v4float
          %c_out = OpVariable %_ptr_Output_v4float Output
          %vert_main = OpFunction %void None %14
         %43 = OpLabel
         %44 = OpVariable %_ptr_Function__arr_v2float_uint_3 Function
         %45 = OpAccessChain %_ptr_Uniform_uint %11 %int_0 %int_4
         %46 = OpLoad %uint %45
         %47 = OpAccessChain %_ptr_Uniform_uint %8 %int_0 %int_0
               OpStore %47 %46
         %48 = OpLoad %int %gl_VertexIndex
         %49 = OpSMod %int %48 %int_3
               OpStore %44 %37
         %50 = OpAccessChain %_ptr_Function_v2float %44 %49
         %51 = OpLoad %v2float %50
         %52 = OpCompositeExtract %float %51 0
         %53 = OpCompositeExtract %float %51 1
         %54 = OpCompositeConstruct %v4float %52 %53 %float_0 %float_1
         %55 = OpAccessChain %_ptr_Output_v4float %2 %int_0
               OpStore %55 %54
               OpReturn
               OpFunctionEnd
          %frag_main = OpFunction %void None %14
         %56 = OpLabel
         %57 = OpAccessChain %_ptr_Uniform_uint %11 %int_0 %int_0
         %58 = OpLoad %uint %57
         %59 = OpAccessChain %_ptr_Uniform_uint %8 %int_0 %int_4
               OpStore %59 %58
               OpReturn
               OpFunctionEnd
        )";
    VkShaderObj vs(this, shader_source, VK_SHADER_STAGE_VERTEX_BIT, SPV_ENV_VULKAN_1_0, SPV_SOURCE_ASM, nullptr, "vert_main");
    VkShaderObj fs(this, shader_source, VK_SHADER_STAGE_FRAGMENT_BIT, SPV_ENV_VULKAN_1_0, SPV_SOURCE_ASM, nullptr, "frag_main");

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 3, 1, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-uniformBuffers-06935", 3);  // vertex
    m_errorMonitor->SetDesiredError("VUID-vkCmdDraw-storageBuffers-06936", 3);  // fragment

    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, PartialBoundDescriptorCopy) {
    TEST_DESCRIPTION("Copy the partial bound buffer the descriptor that is used");

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer foo {
            vec4 a;
            vec4 b[4];
        };

        void main() {
            a = b[3];
        }
    )glsl";

    vkt::Buffer buffer(*m_device, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    OneOffDescriptorSet descriptor_set_src(m_device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    descriptor_set_src.WriteDescriptorBufferInfo(0, buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set_src.UpdateDescriptorSets();

    OneOffDescriptorSet descriptor_set_dst(m_device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set_dst.layout_});

    VkCopyDescriptorSet descriptor_copy = vku::InitStructHelper();
    descriptor_copy.srcSet = descriptor_set_src.set_;
    descriptor_copy.srcBinding = 0;
    descriptor_copy.dstSet = descriptor_set_dst.set_;
    descriptor_copy.dstBinding = 0;
    descriptor_copy.dstArrayElement = 0;
    descriptor_copy.descriptorCount = 1;
    vk::UpdateDescriptorSets(device(), 0, nullptr, 1, &descriptor_copy);

    CreateComputePipelineHelper pipe(*this);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_1);
    pipe.CreateComputePipeline();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set_dst.set_, 0,
                              nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, DeviceGeneratedCommandsCompute) {
    SetTargetApiVersion(VK_API_VERSION_1_2);
    AddRequiredExtensions(VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME);
    AddRequiredExtensions(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::deviceGeneratedCommands);
    AddRequiredFeature(vkt::Feature::bufferDeviceAddress);

    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    VkPhysicalDeviceDeviceGeneratedCommandsPropertiesEXT dgc_props = vku::InitStructHelper();
    GetPhysicalDeviceProperties2(dgc_props);
    if ((dgc_props.supportedIndirectCommandsShaderStagesPipelineBinding & VK_SHADER_STAGE_COMPUTE_BIT) == 0) {
        GTEST_SKIP() << "VK_SHADER_STAGE_COMPUTE_BIT is not supported.";
    }

    VkIndirectCommandsLayoutTokenEXT token;
    token = vku::InitStructHelper();
    token.type = VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT;
    token.offset = 0;

    VkIndirectCommandsLayoutCreateInfoEXT command_layout_ci = vku::InitStructHelper();
    command_layout_ci.shaderStages = VK_SHADER_STAGE_COMPUTE_BIT;
    command_layout_ci.pipelineLayout = VK_NULL_HANDLE;
    command_layout_ci.tokenCount = 1;
    command_layout_ci.pTokens = &token;
    vkt::IndirectCommandsLayout command_layout(*m_device, command_layout_ci);

    char const *shader_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer ssbo {
            uint x[];
        };
        void main() {
            x[48] = 0;
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
    pipe.cs_ = VkShaderObj(this, shader_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_1);
    pipe.CreateComputePipeline();

    vkt::Buffer ssbo_buffer(*m_device, 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, ssbo_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_.UpdateDescriptorSets();

    VkGeneratedCommandsPipelineInfoEXT pipeline_info = vku::InitStructHelper();
    pipeline_info.pipeline = pipe;

    VkMemoryAllocateFlagsInfo allocate_flag_info = vku::InitStructHelper();
    allocate_flag_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    vkt::Buffer block_buffer(*m_device, 64, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, kHostVisibleMemProps, &allocate_flag_info);

    VkDeviceSize pre_process_size = 0;
    {
        VkGeneratedCommandsMemoryRequirementsInfoEXT dgc_mem_reqs = vku::InitStructHelper(&pipeline_info);
        dgc_mem_reqs.indirectCommandsLayout = command_layout;
        dgc_mem_reqs.indirectExecutionSet = VK_NULL_HANDLE;
        dgc_mem_reqs.maxSequenceCount = 1;
        VkMemoryRequirements2 mem_reqs2 = vku::InitStructHelper();
        vk::GetGeneratedCommandsMemoryRequirementsEXT(device(), &dgc_mem_reqs, &mem_reqs2);
        pre_process_size = mem_reqs2.memoryRequirements.size;
    }

    VkBufferUsageFlags2CreateInfo buffer_usage_flags = vku::InitStructHelper();
    buffer_usage_flags.usage = VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBufferCreateInfo buffer_ci = vku::InitStructHelper(&buffer_usage_flags);
    buffer_ci.size = pre_process_size;
    vkt::Buffer pre_process_buffer(*m_device, buffer_ci, 0, &allocate_flag_info);

    VkDispatchIndirectCommand *block_buffer_ptr = (VkDispatchIndirectCommand *)block_buffer.Memory().Map();
    block_buffer_ptr->x = 1;
    block_buffer_ptr->y = 1;
    block_buffer_ptr->z = 1;

    VkGeneratedCommandsInfoEXT generated_commands_info = vku::InitStructHelper(&pipeline_info);
    generated_commands_info.shaderStages = VK_SHADER_STAGE_COMPUTE_BIT;
    generated_commands_info.indirectExecutionSet = VK_NULL_HANDLE;
    generated_commands_info.indirectCommandsLayout = command_layout;
    generated_commands_info.indirectAddressSize = sizeof(VkDispatchIndirectCommand);
    generated_commands_info.indirectAddress = block_buffer.Address();
    generated_commands_info.preprocessAddress = pre_process_buffer.Address();
    generated_commands_info.preprocessSize = pre_process_size;
    generated_commands_info.sequenceCountAddress = 0;
    generated_commands_info.maxSequenceCount = 1;

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_.set_, 0, nullptr);
    vk::CmdExecuteGeneratedCommandsEXT(m_command_buffer, false, &generated_commands_info);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdExecuteGeneratedCommandsEXT-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, SpecConstant) {
    SetTargetApiVersion(VK_API_VERSION_1_2);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    char const *cs_source = R"glsl(
        #version 450
        layout(constant_id = 0) const uint index = 2;

        layout(set = 0, binding = 0) buffer foo {
            uint a;
            uint b[4];
        };

        void main() {
            a = b[index];
        }
    )glsl";

    const uint32_t value = 25;
    VkSpecializationMapEntry entry = {0, 0, sizeof(uint32_t)};
    VkSpecializationInfo spec_info = {1, &entry, sizeof(uint32_t), &value};

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_2, SPV_SOURCE_GLSL,
                                             &spec_info);
    pipe.CreateComputePipeline();

    vkt::Buffer in_buffer(*m_device, 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);

    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_.UpdateDescriptorSets();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_.set_, 0, nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, PartialBoundDescriptorSSBO) {
    TEST_DESCRIPTION("Only bound part of a SSBO (with update after bind), and use the part that is not valid");
    SetTargetApiVersion(VK_API_VERSION_1_2);
    AddRequiredFeature(vkt::Feature::descriptorBindingStorageBufferUpdateAfterBind);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    char const *shader_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer foo {
            vec4 a; // offset 0
            vec4 b; // offset 16
            vec4 c; // offset 32 - not bound, can't use
            vec4 d; // offset 48 - not bound, can't use
        };
        void main() {
            a = c;
        }
    )glsl";

    OneOffDescriptorIndexingSet descriptor_set(m_device, {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr,
                                                           VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT}});
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    CreateComputePipelineHelper pipe(*this);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.cs_ = VkShaderObj(this, shader_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_1);
    pipe.CreateComputePipeline();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    vkt::Buffer buffer(*m_device, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    descriptor_set.WriteDescriptorBufferInfo(0, buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.UpdateDescriptorSets();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, VectorArray) {
    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer foo {
            uvec4 a[8]; // stride 16
        };
        void main() {
            a[3].y = 44; // write at byte[52:56]
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 48, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ArrayCopyGLSL) {
    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0, std430) buffer foo {
            uvec4 a;
            uint padding;
            uint b[4]; // b[3] is OOB
            uint c; // offset 36
        };

        void main() {
            uint d[4] = {4, 5, 6, 7};
            b = d;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 32, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ArrayCopySlang) {
    TEST_DESCRIPTION("Note that in slang and array copy is really a struct copy");

    RETURN_IF_SKIP(CheckSlangSupport());

    const char *slang_shader = R"slang(
        struct Bar {
          uint4 a;
          uint pad;
          uint b[4]; // b[3] is OOB
          uint c; // offset 36
        };
        
        [[vk::binding(0, 0)]]
        RWStructuredBuffer<Bar> foo;
        
        [shader("compute")]
        void main() {
          uint d[4] = {4, 5, 6, 7};
          foo[0].b = d;
        }    
    )slang";
    ComputeStorageBufferTest(slang_shader, SPV_SOURCE_SLANG, 32, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ArrayCopyTwoBindingsGLSL) {
    SetTargetApiVersion(VK_API_VERSION_1_2);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0, std430) buffer foo1 {
            uvec4 a;
            uint padding;
            uint b[4];
            uint c;
        };

        layout(set = 0, binding = 1, std430) buffer foo2 {
            uint d;
            uint e[4];
            uvec2 f;
        };

        void main() {
            b = e;
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                          {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}};
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_2);
    pipe.CreateComputePipeline();

    vkt::Buffer in_buffer(*m_device, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    // not enough bound for either
    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 0, 32, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(1, in_buffer, 64, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_.UpdateDescriptorSets();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_.set_, 0, nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ArrayCopyTwoBindingsSlang) {
    TEST_DESCRIPTION("Note that in slang and array copy is really a struct copy");

    RETURN_IF_SKIP(CheckSlangSupport());

    SetTargetApiVersion(VK_API_VERSION_1_2);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    const char *slang_shader = R"slang(
        struct Bar1 {
            uint4 a;
            uint b[4];
            uint c;
        };
        
        struct Bar2 {
            uint4 d;
            uint e[4];
            uint f;
        };
        
        [[vk::binding(0, 0)]]
        RWStructuredBuffer<Bar1> foo1;
        
        [[vk::binding(1, 0)]]
        RWStructuredBuffer<Bar2> foo2;
        
        [shader("compute")]
        void main() {
            foo1[0].b = foo2[0].e;
        }
    )slang";

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                          {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}};
    pipe.cs_ = VkShaderObj(this, slang_shader, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_2, SPV_SOURCE_SLANG);
    pipe.CreateComputePipeline();

    vkt::Buffer in_buffer(*m_device, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 0, 64, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(1, in_buffer, 64, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    pipe.descriptor_set_.UpdateDescriptorSets();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_.set_, 0, nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

// https://github.com/KhronosGroup/glslang/issues/3892
TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, DISABLED_StructCopyGLSL) {
    char const *cs_source = R"glsl(
        #version 450

        struct Bar {
            uint x;
            uint y;
            uint z[2];
        };

        layout(set = 0, binding = 0, std430) buffer foo {
            uvec4 a;
            uint padding;
            Bar b; // size 16 at offset 20
            uint c;
        };

        void main() {
            Bar new_bar;
            b = new_bar;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 32, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, StructCopyGLSL2) {
    char const *cs_source = R"glsl(
        #version 450

        struct Bar {
            vec4 x;
        };

        layout(set = 0, binding = 0, std430) buffer foo {
            uvec4 a;
            uint padding;
            Bar b; // size 16 at offset 20
            uint c;
        };

        void main() {
            Bar new_bar;
            b = new_bar;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 32, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, StructCopyGLSL3) {
    char const *cs_source = R"glsl(
        #version 450

        struct Bar2 {
            vec4 x;
        };

        struct Bar {
            uint x;
            Bar2 y;
        };

        layout(set = 0, binding = 0, std430) buffer foo {
            uvec4 a;
            Bar b; // size 32 at offset 16
            uint c;
        };

        void main() {
            Bar new_bar;
            b = new_bar;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 32, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, StructCopySlang) {
    RETURN_IF_SKIP(CheckSlangSupport());

    const char *slang_shader = R"slang(
        struct Bar {
          uint x;
          uint y;
          uint z[2];
        };
        
        struct FooBuffer {
          float4 a;
          Bar b;
          uint c;
        };
        
        [[vk::binding(0, 0)]]
        RWStructuredBuffer<FooBuffer> foo;
        
        [shader("compute")]
        void main() {
          foo[1].b = foo[0].b;
        }
    )slang";

    ComputeStorageBufferTest(slang_shader, SPV_SOURCE_SLANG, 72, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ChainOfAccessChains) {
    TEST_DESCRIPTION("Slang can sometimes generate a single OpAccessChain like GLSL/HLSL");

    RETURN_IF_SKIP(CheckSlangSupport());

    const char *slang_shader = R"slang(
        struct Bar {
            uint a;
            uint d[4]; // this really ends up being a 1 element struct with the array in it
        };
        
        [[vk::binding(0, 0)]]
        RWStructuredBuffer<Bar> foo; // 20 byte stride
        
        [shader("compute")]
        void main() {
            foo[1].d[3] = 44;
        }
    )slang";

    ComputeStorageBufferTest(slang_shader, SPV_SOURCE_SLANG, 32, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, AtomicStore) {
    char const *cs_source = R"glsl(
        #version 450
        #extension GL_KHR_memory_scope_semantics : enable
        layout(set = 0, binding = 0, std430) buffer foo {
            uvec4 a;
            uint b;
        };

        void main() {
            atomicStore(b, 0u, gl_ScopeDevice, gl_StorageSemanticsBuffer, gl_SemanticsRelaxed);
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 16, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, AtomicLoad) {
    char const *cs_source = R"glsl(
        #version 450
        #extension GL_KHR_memory_scope_semantics : enable
        layout(set = 0, binding = 0, std430) buffer foo {
            uvec4 a;
            uvec4 b;
        };

        void main() {
            a.x = atomicLoad(b.x, gl_ScopeDevice, gl_StorageSemanticsBuffer, gl_SemanticsRelaxed);
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 16, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, AtomicExchange) {
    char const *cs_source = R"glsl(
        #version 450
        #extension GL_KHR_memory_scope_semantics : enable
        layout(set = 0, binding = 0, std430) buffer foo {
            uvec4 a;
            uint b;
        };

        void main() {
            atomicExchange(b, a.x);
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 16, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, AtomicsDescriptorIndex) {
    SetTargetApiVersion(VK_API_VERSION_1_2);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    char const *cs_source = R"glsl(
        #version 450
        #extension GL_KHR_memory_scope_semantics : enable
        layout(set = 0, binding = 0, std430) buffer SSBO {
            uvec4 a;
            uint b;
        } ssbo[2];

        void main() {
            atomicStore(ssbo[0].b, 0u, gl_ScopeDevice, gl_StorageSemanticsBuffer, gl_SemanticsRelaxed); // valid
            atomicStore(ssbo[1].b, 0u, gl_ScopeDevice, gl_StorageSemanticsBuffer, gl_SemanticsRelaxed); // invalid
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, VK_SHADER_STAGE_ALL, nullptr};
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_2);
    pipe.CreateComputePipeline();

    vkt::Buffer in_buffer(*m_device, 128, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 0, 64, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 64, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
    pipe.descriptor_set_.UpdateDescriptorSets();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_.set_, 0, nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, DescriptorIndexSlang) {
    RETURN_IF_SKIP(CheckSlangSupport());

    SetTargetApiVersion(VK_API_VERSION_1_2);
    RETURN_IF_SKIP(InitGpuAvFramework());
    RETURN_IF_SKIP(InitState());

    const char *slang_shader = R"slang(
        struct Bar {
          uint4 a;
          uint b[4];
          uint c;
          uint d;
        };
        
        [[vk::binding(0, 0)]]
        RWStructuredBuffer<Bar> foo[2]; // stride of 48 bytes
        
        [shader("compute")]
        void main() {
          foo[1][1].d = 0;
        }
    )slang";

    CreateComputePipelineHelper pipe(*this);
    pipe.dsl_bindings_[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, VK_SHADER_STAGE_ALL, nullptr};
    pipe.cs_ = VkShaderObj(this, slang_shader, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_2, SPV_SOURCE_SLANG);
    pipe.CreateComputePipeline();

    vkt::Buffer in_buffer(*m_device, 96, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, kHostVisibleMemProps);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0);
    pipe.descriptor_set_.WriteDescriptorBufferInfo(0, in_buffer, 0, 80, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
    pipe.descriptor_set_.UpdateDescriptorSets();

    m_command_buffer.Begin();
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_, 0, 1,
                              &pipe.descriptor_set_.set_, 0, nullptr);
    vk::CmdDispatch(m_command_buffer, 1, 1, 1);
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDispatch-storageBuffers-06936");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, Loops) {
    TEST_DESCRIPTION("Invalid during last iteration of a loop");

    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer Input {
            uint result;
            uint data[64]; // [63] will be OOB
        };

        void main() {
            uint x = 0;
            for (uint i = 0; i < 64; i++) {
                x += data[i];
            }
            result = x;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 256, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, LoopsNested) {
    TEST_DESCRIPTION("Invalid during last iteration of a loop");

    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer Input {
            uint result;
            uint data[64]; // [63] will be OOB
        };

        void main() {
            uint x = 0;
            for (uint i = 0; i < 8; i++) {
                for (uint j = 0; j < 8; j++) {
                    x += data[(i * 8) + j];
                }
            }
            result = x;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 256, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, LoopsHoistable) {
    TEST_DESCRIPTION("Invalid during each iteration of a loop, but not dependent on the loop itself");

    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer Input {
            uint result;
            uint data[64]; // [63] will be OOB
        };

        void main() {
            uint x = 0;
            for (uint i = 0; i < 4; i++) {
                x += data[63] + i;
            }
            result = x;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 256, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, LoopsNestedHoistable) {
    TEST_DESCRIPTION("Invalid during each iteration of a loop, but not dependent on the loop itself");

    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer Input {
            uint result;
            uint data[64]; // [63] will be OOB
        };

        void main() {
            uint x = 0;
            for (uint i = 0; i < 2; i++) {
                for (uint j = 0; j < 2; j++) {
                    x += data[63] + i + j;
                }
            }
            result = x;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 256, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, LoopsWithBranch) {
    TEST_DESCRIPTION("Invalid during last iteration of a loop, but has branch in it");

    char const *cs_source = R"glsl(
        #version 450
        layout(set = 0, binding = 0) buffer Input {
            uint result;
            uint data[64]; // [63] will be OOB
        };

        void main() {
            uint x = 0;
            for (uint i = 1; i < 64; i++) {
                if (i / 2 == 0) {
                    x += data[i - 1];
                } else {
                    x += data[i];
                }
            }
            result = x;
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 256, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, ComplexCalculate) {
    TEST_DESCRIPTION("from Granite meshopt-sandbox test");
    char const *cs_source = R"glsl(
        #version 450

        layout(set = 0, binding = 0, std430) buffer SSBO {
            uint data[]; // holds 450 items
        } payload;

        uvec2 meshlet_decode2(uint offset_in_words, uint index, uint bit_count) {
            uint start_bit = index * bit_count * 2;
            // start_word will be 449 which is the last item
            uint start_word = offset_in_words + start_bit / 32u;
            start_bit &= 31u;
            uint word0 = payload.data[start_word];
            uint word1 = payload.data[start_word + 1u];
            return uvec2(word0, word1);
        }

        void main() {
            meshlet_decode2(444, 14, 6);
            meshlet_decode2(444, 15, 6);
        }
    )glsl";
    ComputeStorageBufferTest(cs_source, SPV_SOURCE_GLSL, 1800, "VUID-vkCmdDispatch-storageBuffers-06936");
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, TaskShader) {
    SetTargetApiVersion(VK_API_VERSION_1_3);
    AddRequiredExtensions(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::maintenance4);
    AddRequiredFeature(vkt::Feature::meshShader);
    AddRequiredFeature(vkt::Feature::taskShader);
    RETURN_IF_SKIP(InitGpuAvFramework({}, false));
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    const char *task_source = R"glsl(
        #version 460
        #extension GL_EXT_mesh_shader : enable
        taskPayloadSharedEXT uint payload;

        layout(set = 0, binding = 0) readonly uniform UBO {
            uvec3 a;
            uint b; // offset 12
        };

        void main() {
            if (gl_GlobalInvocationID.x < b) {
                payload = 0;
            }
            EmitMeshTasksEXT(1u, 1u, 1u);
        }
    )glsl";

    VkShaderObj ts(this, task_source, VK_SHADER_STAGE_TASK_BIT_EXT, SPV_ENV_VULKAN_1_3);
    VkShaderObj ms(this, kMeshMinimalGlsl, VK_SHADER_STAGE_MESH_BIT_EXT, SPV_ENV_VULKAN_1_3);
    VkShaderObj fs(this, kFragmentMinimalGlsl, VK_SHADER_STAGE_FRAGMENT_BIT, SPV_ENV_VULKAN_1_3);

    OneOffDescriptorSet descriptor_set(m_device, {
                                                     {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                 });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    vkt::Buffer ubo_buffer(*m_device, 8, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    descriptor_set.WriteDescriptorBufferInfo(0, ubo_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.UpdateDescriptorSets();

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {ts.GetStageCreateInfo(), ms.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDrawMeshTasksEXT(m_command_buffer, 1, 1, 1);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDrawMeshTasksEXT-uniformBuffers-06935");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}

TEST_F(NegativeGpuAVDescriptorClassGeneralBuffer, MeshTaskIndirect) {
    TEST_DESCRIPTION("https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/10036");
    SetTargetApiVersion(VK_API_VERSION_1_3);
    AddRequiredExtensions(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::maintenance4);
    AddRequiredFeature(vkt::Feature::meshShader);
    AddRequiredFeature(vkt::Feature::taskShader);
    AddRequiredFeature(vkt::Feature::fragmentStoresAndAtomics);
    RETURN_IF_SKIP(InitGpuAvFramework({}, false));
    RETURN_IF_SKIP(InitState());
    InitRenderTarget();

    vkt::Buffer draw_buffer(*m_device, sizeof(VkDrawMeshTasksIndirectCommandEXT), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                            kHostVisibleMemProps);
    auto *draw_ptr = static_cast<VkDrawMeshTasksIndirectCommandEXT *>(draw_buffer.Memory().Map());
    draw_ptr->groupCountX = 1;
    draw_ptr->groupCountY = 1;
    draw_ptr->groupCountZ = 1;

    const char *task_source = R"glsl(
        #version 460
        #extension GL_EXT_mesh_shader : enable
        taskPayloadSharedEXT uint payload;

        layout(set = 0, binding = 0) readonly buffer DispatchStorage {
            uvec3 Command;
            uint Count;
        };

        void main() {
            if (gl_GlobalInvocationID.x < Count) {
                payload = 0;
            }
            EmitMeshTasksEXT(1u, 1u, 1u);
        }
    )glsl";

    char const *mesh_source = R"glsl(
        #version 450
        #extension GL_EXT_mesh_shader : require
        layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
        layout(max_vertices = 3, max_primitives = 1) out;
        layout(triangles) out;
        taskPayloadSharedEXT uint payload;

        layout(set = 0, binding = 1) readonly uniform CamUBO {
            mat4 Mat;
        };

        void main() {
            mat4 m = Mat;
            SetMeshOutputsEXT(3, 1);
            gl_MeshVerticesEXT[0].gl_Position = vec4(-1.0, -1.0, 0.0, 1.0);
            gl_MeshVerticesEXT[1].gl_Position = vec4(1.0, -1.0, 0.0, 1.0);
            gl_MeshVerticesEXT[2].gl_Position = vec4(0.0, 1.0, 0.0, 1.0);
            gl_PrimitiveTriangleIndicesEXT[0] =  uvec3(0, 1, 2);
        }
    )glsl";

    char const *frag_source = R"glsl(
        #version 460
        layout(location = 0) out vec4 uFragColor;
        layout(set = 0, binding = 2) readonly uniform UBO {
            vec4 color;
        };
        void main(){
            uFragColor = color;
        }
    )glsl";

    VkShaderObj ts(this, task_source, VK_SHADER_STAGE_TASK_BIT_EXT, SPV_ENV_VULKAN_1_3);
    VkShaderObj ms(this, mesh_source, VK_SHADER_STAGE_MESH_BIT_EXT, SPV_ENV_VULKAN_1_3);
    VkShaderObj fs(this, frag_source, VK_SHADER_STAGE_FRAGMENT_BIT, SPV_ENV_VULKAN_1_3);

    vkt::Buffer ssbo_buffer(*m_device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    vkt::Buffer ubo_buffer(*m_device, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    OneOffDescriptorSet descriptor_set(m_device, {
                                                     {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                     {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                     {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                                 });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorBufferInfo(0, ssbo_buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    descriptor_set.WriteDescriptorBufferInfo(1, ubo_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.WriteDescriptorBufferInfo(2, ubo_buffer, 0, VK_WHOLE_SIZE);
    descriptor_set.UpdateDescriptorSets();

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {ts.GetStageCreateInfo(), ms.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDrawMeshTasksIndirectEXT(m_command_buffer, draw_buffer, 0, 1, sizeof(VkDrawMeshTasksIndirectCommandEXT));
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();

    m_errorMonitor->SetDesiredError("VUID-vkCmdDrawMeshTasksIndirectEXT-uniformBuffers-06935");
    m_default_queue->SubmitAndWait(m_command_buffer);
    m_errorMonitor->VerifyFound();
}