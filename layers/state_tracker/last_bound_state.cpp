/* Copyright (c) 2015-2025 The Khronos Group Inc.
 * Copyright (c) 2015-2025 Valve Corporation
 * Copyright (c) 2015-2025 LunarG, Inc.
 * Copyright (C) 2015-2025 Google Inc.
 * Modifications Copyright (C) 2020 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "last_bound_state.h"
#include <vulkan/vulkan_core.h>
#include <cassert>
#include "containers/container_utils.h"
#include "state_tracker/pipeline_state.h"
#include "generated/dynamic_state_helper.h"
#include "state_tracker/descriptor_sets.h"
#include "state_tracker/cmd_buffer_state.h"
#include "state_tracker/shader_object_state.h"
#include "chassis/chassis_modification_state.h"
#include "utils/vk_api_utils.h"

void LastBound::UnbindAndResetPushDescriptorSet(std::shared_ptr<vvl::DescriptorSet> &&ds) {
    if (push_descriptor_set) {
        for (auto &ds_slot : ds_slots) {
            if (ds_slot.ds_state == push_descriptor_set) {
                cb_state.RemoveChild(ds_slot.ds_state);
                ds_slot.ds_state.reset();
            }
        }
    }
    cb_state.AddChild(ds);
    push_descriptor_set = std::move(ds);
}

bool LastBound::IsDynamic(const CBDynamicState state) const { return !pipeline_state || pipeline_state->IsDynamic(state); }

void LastBound::Reset() {
    pipeline_state = nullptr;
    desc_set_pipeline_layout.reset();
    if (push_descriptor_set) {
        cb_state.RemoveChild(push_descriptor_set);
        push_descriptor_set->Destroy();
    }
    push_descriptor_set.reset();
    ds_slots.clear();
}

bool LastBound::IsDepthTestEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_DEPTH_TEST_ENABLE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_DEPTH_TEST_ENABLE)) {
            return cb_state.dynamic_state_value.depth_test_enable;
        }
    } else {
        if (pipeline_state->DepthStencilState()) {
            return pipeline_state->DepthStencilState()->depthTestEnable;
        }
    }
    return false;
}

bool LastBound::IsDepthBoundTestEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE)) {
            return cb_state.dynamic_state_value.depth_bounds_test_enable;
        }
    } else {
        if (pipeline_state->DepthStencilState()) {
            return pipeline_state->DepthStencilState()->depthBoundsTestEnable;
        }
    }
    return false;
}

bool LastBound::IsDepthWriteEnable() const {
    // "Depth writes are always disabled when depthTestEnable is VK_FALSE"
    if (!IsDepthTestEnable()) {
        return false;
    }
    if (IsDynamic(CB_DYNAMIC_STATE_DEPTH_WRITE_ENABLE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_DEPTH_WRITE_ENABLE)) {
            return cb_state.dynamic_state_value.depth_write_enable;
        }
    } else {
        if (pipeline_state->DepthStencilState()) {
            return pipeline_state->DepthStencilState()->depthWriteEnable;
        }
    }
    return false;
}

bool LastBound::IsDepthBiasEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_DEPTH_BIAS_ENABLE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_DEPTH_BIAS_ENABLE)) {
            return cb_state.dynamic_state_value.depth_bias_enable;
        }
    } else {
        if (pipeline_state->RasterizationState()) {
            return pipeline_state->RasterizationState()->depthBiasEnable;
        }
    }
    return false;
}

bool LastBound::IsDepthClampEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.depth_clamp_enable;
        }
    } else {
        if (pipeline_state->RasterizationState()) {
            return pipeline_state->RasterizationState()->depthClampEnable;
        }
    }
    return false;
}

bool LastBound::IsStencilTestEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_STENCIL_TEST_ENABLE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_STENCIL_TEST_ENABLE)) {
            return cb_state.dynamic_state_value.stencil_test_enable;
        }
    } else {
        if (pipeline_state->DepthStencilState()) {
            return pipeline_state->DepthStencilState()->stencilTestEnable;
        }
    }
    return false;
}

VkStencilOpState LastBound::GetStencilOpStateFront() const {
    VkStencilOpState front = {};
    if (pipeline_state) {
        front = pipeline_state->DepthStencilState()->front;
    }
    if (IsDynamic(CB_DYNAMIC_STATE_STENCIL_WRITE_MASK)) {
        front.writeMask = cb_state.dynamic_state_value.write_mask_front;
    }
    if (IsDynamic(CB_DYNAMIC_STATE_STENCIL_OP)) {
        front.failOp = cb_state.dynamic_state_value.fail_op_front;
        front.passOp = cb_state.dynamic_state_value.pass_op_front;
        front.depthFailOp = cb_state.dynamic_state_value.depth_fail_op_front;
    }
    return front;
}

VkStencilOpState LastBound::GetStencilOpStateBack() const {
    VkStencilOpState back = {};
    if (pipeline_state) {
        back = pipeline_state->DepthStencilState()->back;
    }
    if (IsDynamic(CB_DYNAMIC_STATE_STENCIL_WRITE_MASK)) {
        back.writeMask = cb_state.dynamic_state_value.write_mask_back;
    }
    if (IsDynamic(CB_DYNAMIC_STATE_STENCIL_OP)) {
        back.failOp = cb_state.dynamic_state_value.fail_op_back;
        back.passOp = cb_state.dynamic_state_value.pass_op_back;
        back.depthFailOp = cb_state.dynamic_state_value.depth_fail_op_back;
    }
    return back;
}

VkSampleCountFlagBits LastBound::GetRasterizationSamples() const {
    // For given pipeline, return number of MSAA samples, or one if MSAA disabled
    VkSampleCountFlagBits rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    if (IsDynamic(CB_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT)) {
        rasterization_samples = cb_state.dynamic_state_value.rasterization_samples;
    } else {
        if (auto ms_state = pipeline_state->MultisampleState()) {
            rasterization_samples = ms_state->rasterizationSamples;
        }
    }
    return rasterization_samples;
}

bool LastBound::IsRasterizationDisabled() const {
    if (IsDynamic(CB_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE)) {
            return cb_state.dynamic_state_value.rasterizer_discard_enable;
        }
    } else {
        return (pipeline_state->RasterizationDisabled());
    }
    return false;
}

bool LastBound::IsLogicOpEnabled() const {
    if (IsDynamic(CB_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.logic_op_enable;
        }
    } else {
        return pipeline_state->ColorBlendState() && pipeline_state->ColorBlendState()->logicOpEnable;
    }
    return false;
}

VkColorComponentFlags LastBound::GetColorWriteMask(uint32_t i) const {
    if (IsDynamic(CB_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT)) {
        if (i < cb_state.dynamic_state_value.color_write_masks.size()) {
            return cb_state.dynamic_state_value.color_write_masks[i];
        }
    } else {
        if (pipeline_state->ColorBlendState() && i < pipeline_state->ColorBlendState()->attachmentCount) {
            return pipeline_state->ColorBlendState()->pAttachments[i].colorWriteMask;
        }
    }
    return (VkColorComponentFlags)0u;
}

bool LastBound::IsColorWriteEnabled(uint32_t i) const {
    if (IsDynamic(CB_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.color_write_enabled[i];
        }
    } else {
        if (pipeline_state->ColorBlendState()) {
            auto color_write =
                vku::FindStructInPNextChain<VkPipelineColorWriteCreateInfoEXT>(pipeline_state->ColorBlendState()->pNext);
            if (color_write && i < color_write->attachmentCount) {
                return color_write->pColorWriteEnables[i];
            }
        }
    }
    return true;
}

bool LastBound::IsColorBlendEnabled(uint32_t i) const {
    if (IsDynamic(CB_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.color_blend_enabled[i];
        }
    } else {
        if (pipeline_state->ColorBlendState() && i < pipeline_state->ColorBlendState()->attachmentCount) {
            return pipeline_state->ColorBlendState()->pAttachments[i].blendEnable;
        }
    }
    return true;
}

std::string LastBound::DescribeColorBlendEnabled(uint32_t i) const {
    std::stringstream ss;
    if (IsDynamic(CB_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT)) {
            ss << "vkCmdSetColorBlendEnableEXT::pColorBlendEnables[" << i << "] is ";
            ss << (cb_state.dynamic_state_value.color_blend_enabled[i] ? "VK_TRUE" : "VK_FALSE");
        }
    } else {
        if (pipeline_state->ColorBlendState() && i < pipeline_state->ColorBlendState()->attachmentCount) {
            ss << "VkPipelineColorBlendStateCreateInfo::pAttachments[" << i << "].blendEnable is ";
            ss << (pipeline_state->ColorBlendState()->pAttachments[i].blendEnable ? "VK_TRUE" : "VK_FALSE");
        }
    }
    return ss.str();
}

bool LastBound::IsBlendConstantsEnabled(uint32_t i) const {
    static constexpr std::array<VkBlendFactor, 4> const_factors = {
        VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR, VK_BLEND_FACTOR_CONSTANT_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA};
    if (IsDynamic(CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT)) {
            if (i >= cb_state.dynamic_state_value.color_blend_equations.size()) {
                return false;  // this color attachment was set with vkCmdSetColorBlendAdvancedEXT instead
            }
            const VkColorBlendEquationEXT &eq = cb_state.dynamic_state_value.color_blend_equations[i];
            return IsValueIn(eq.srcColorBlendFactor, const_factors) || IsValueIn(eq.dstColorBlendFactor, const_factors) ||
                   IsValueIn(eq.srcAlphaBlendFactor, const_factors) || IsValueIn(eq.dstAlphaBlendFactor, const_factors);
        }
    } else {
        if (pipeline_state->ColorBlendState() && i < pipeline_state->ColorBlendState()->attachmentCount) {
            const VkPipelineColorBlendAttachmentState &eq = pipeline_state->ColorBlendState()->pAttachments[i];
            return IsValueIn(eq.srcColorBlendFactor, const_factors) || IsValueIn(eq.dstColorBlendFactor, const_factors) ||
                   IsValueIn(eq.srcAlphaBlendFactor, const_factors) || IsValueIn(eq.dstAlphaBlendFactor, const_factors);
        }
    }
    return false;
}

bool LastBound::IsDualBlending(uint32_t i) const {
    if (IsDynamic(CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT)) {
            if (i >= cb_state.dynamic_state_value.color_blend_equations.size()) {
                return false;  // this color attachment was set with vkCmdSetColorBlendAdvancedEXT instead
            }
            const VkColorBlendEquationEXT &eq = cb_state.dynamic_state_value.color_blend_equations[i];
            return IsSecondaryColorInputBlendFactor(eq.srcColorBlendFactor) ||
                   IsSecondaryColorInputBlendFactor(eq.dstColorBlendFactor) ||
                   IsSecondaryColorInputBlendFactor(eq.srcAlphaBlendFactor) ||
                   IsSecondaryColorInputBlendFactor(eq.dstAlphaBlendFactor);
        }
    } else {
        if (pipeline_state->ColorBlendState() && i < pipeline_state->ColorBlendState()->attachmentCount) {
            const VkPipelineColorBlendAttachmentState &eq = pipeline_state->ColorBlendState()->pAttachments[i];
            return IsSecondaryColorInputBlendFactor(eq.srcColorBlendFactor) ||
                   IsSecondaryColorInputBlendFactor(eq.dstColorBlendFactor) ||
                   IsSecondaryColorInputBlendFactor(eq.srcAlphaBlendFactor) ||
                   IsSecondaryColorInputBlendFactor(eq.dstAlphaBlendFactor);
        }
    }
    return false;
}

std::string LastBound::DescribeBlendFactorEquation(uint32_t i) const {
    std::stringstream ss;
    if (IsDynamic(CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT)) {
            const VkColorBlendEquationEXT &eq = cb_state.dynamic_state_value.color_blend_equations[i];
            ss << "The following are set by vkCmdSetColorBlendEquationEXT::pColorBlendEquations[" << i << "]\n";
            ss << "  srcColorBlendFactor = " << string_VkBlendFactor(eq.srcColorBlendFactor) << "\n";
            ss << "  dstColorBlendFactor = " << string_VkBlendFactor(eq.dstColorBlendFactor) << "\n";
            ss << "  srcAlphaBlendFactor = " << string_VkBlendFactor(eq.srcAlphaBlendFactor) << "\n";
            ss << "  dstAlphaBlendFactor = " << string_VkBlendFactor(eq.dstAlphaBlendFactor) << "\n";
        }
    } else {
        if (pipeline_state->ColorBlendState() && i < pipeline_state->ColorBlendState()->attachmentCount) {
            const VkPipelineColorBlendAttachmentState &eq = pipeline_state->ColorBlendState()->pAttachments[i];
            ss << "The following are set by VkPipelineColorBlendStateCreateInfo::pAttachments[" << i << "]\n";
            ss << "  srcColorBlendFactor = " << string_VkBlendFactor(eq.srcColorBlendFactor) << "\n";
            ss << "  dstColorBlendFactor = " << string_VkBlendFactor(eq.dstColorBlendFactor) << "\n";
            ss << "  srcAlphaBlendFactor = " << string_VkBlendFactor(eq.srcAlphaBlendFactor) << "\n";
            ss << "  dstAlphaBlendFactor = " << string_VkBlendFactor(eq.dstAlphaBlendFactor) << "\n";
        }
    }
    return ss.str();
}

VkCullModeFlags LastBound::GetCullMode() const {
    if (IsDynamic(CB_DYNAMIC_STATE_CULL_MODE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_CULL_MODE)) {
            return cb_state.dynamic_state_value.cull_mode;
        }
    } else {
        if (auto raster_state = pipeline_state->RasterizationState()) {
            return raster_state->cullMode;
        }
    }
    return VK_CULL_MODE_NONE;
}

VkConservativeRasterizationModeEXT LastBound::GetConservativeRasterizationMode() const {
    if (IsDynamic(CB_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT)) {
            return cb_state.dynamic_state_value.conservative_rasterization_mode;
        }
    } else {
        if (const auto rasterization_conservative_state_ci =
                vku::FindStructInPNextChain<VkPipelineRasterizationConservativeStateCreateInfoEXT>(
                    pipeline_state->RasterizationStatePNext())) {
            return rasterization_conservative_state_ci->conservativeRasterizationMode;
        }
    }
    return VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT;
}

bool LastBound::IsSampleLocationsEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.sample_locations_enable;
        }
    } else {
        if (auto ms_state = pipeline_state->MultisampleState()) {
            if (const auto *sample_location_state =
                    vku::FindStructInPNextChain<VkPipelineSampleLocationsStateCreateInfoEXT>(ms_state->pNext)) {
                return sample_location_state->sampleLocationsEnable;
            }
        }
    }
    return false;
}

bool LastBound::IsExclusiveScissorEnabled() const {
    if (IsDynamic(CB_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_ENABLE_NV)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_EXCLUSIVE_SCISSOR_ENABLE_NV)) {
            for (uint32_t i = 0; i < cb_state.dynamic_state_value.exclusive_scissor_enable_count; ++i) {
                if (cb_state.dynamic_state_value
                        .exclusive_scissor_enables[cb_state.dynamic_state_value.exclusive_scissor_enable_first + i]) {
                    return true;
                }
            }
        }
    } else {
        return true;  // no pipeline state, but if not dynamic, defaults to being enabled
    }
    return false;
}

bool LastBound::IsCoverageToColorEnabled() const {
    if (IsDynamic(CB_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COVERAGE_TO_COLOR_ENABLE_NV)) {
            return cb_state.dynamic_state_value.coverage_to_color_enable;
        }
    } else {
        if (auto ms_state = pipeline_state->MultisampleState()) {
            if (const auto *coverage_to_color_state =
                    vku::FindStructInPNextChain<VkPipelineCoverageToColorStateCreateInfoNV>(ms_state->pNext)) {
                return coverage_to_color_state->coverageToColorEnable;
            }
        }
    }
    return false;
}

bool LastBound::IsCoverageModulationTableEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COVERAGE_MODULATION_TABLE_ENABLE_NV)) {
            return cb_state.dynamic_state_value.coverage_modulation_table_enable;
        }
    } else {
        if (auto ms_state = pipeline_state->MultisampleState()) {
            if (const auto *coverage_modulation_state =
                    vku::FindStructInPNextChain<VkPipelineCoverageModulationStateCreateInfoNV>(ms_state->pNext)) {
                return coverage_modulation_state->coverageModulationTableEnable;
            }
        }
    }
    return false;
}

bool LastBound::IsStippledLineEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.stippled_line_enable;
        }
    } else {
        if (const auto line_state_ci = vku::FindStructInPNextChain<VkPipelineRasterizationLineStateCreateInfo>(
                pipeline_state->RasterizationStatePNext())) {
            return line_state_ci->stippledLineEnable;
        }
    }
    return false;
}

bool LastBound::IsDiscardRectangleEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.discard_rectangle_enable;
        }
    } else {
        // VK_EXT_discard_rectangles had a special v2 added right away to give it dynamic state
        // "If the VK_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT dynamic state is not enabled for the pipeline the presence of this
        // structure in the VkGraphicsPipelineCreateInfo chain, and a discardRectangleCount greater than zero, implicitly enables
        // discard rectangles in the pipeline"
        const void *pipeline_pnext = pipeline_state->GetCreateInfoPNext();
        if (const auto *discard_rectangle_state =
                vku::FindStructInPNextChain<VkPipelineDiscardRectangleStateCreateInfoEXT>(pipeline_pnext)) {
            return discard_rectangle_state->discardRectangleCount > 0;
        }
    }
    return false;
}

bool LastBound::IsShadingRateImageEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_SHADING_RATE_IMAGE_ENABLE_NV)) {
            return cb_state.dynamic_state_value.shading_rate_image_enable;
        }
    } else {
        if (auto viewport_state = pipeline_state->ViewportState()) {
            if (const auto *shading_rate_image_state =
                    vku::FindStructInPNextChain<VkPipelineViewportShadingRateImageStateCreateInfoNV>(viewport_state->pNext)) {
                return shading_rate_image_state->shadingRateImageEnable;
            }
        }
    }
    return false;
}

bool LastBound::IsViewportWScalingEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_VIEWPORT_W_SCALING_ENABLE_NV)) {
            return cb_state.dynamic_state_value.viewport_w_scaling_enable;
        }
    } else {
        if (auto viewport_state = pipeline_state->ViewportState()) {
            if (const auto *viewport_w_scaling_state =
                    vku::FindStructInPNextChain<VkPipelineViewportWScalingStateCreateInfoNV>(viewport_state->pNext)) {
                return viewport_w_scaling_state->viewportWScalingEnable;
            }
        }
    }
    return false;
}

bool LastBound::IsPrimitiveRestartEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE)) {
            return cb_state.dynamic_state_value.primitive_restart_enable;
        }
    } else {
        if (auto ia_state = pipeline_state->InputAssemblyState()) {
            return ia_state->primitiveRestartEnable == VK_TRUE;
        }
    }
    return false;
}

bool LastBound::IsAlphaToCoverageEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.alpha_to_coverage_enable;
        }
    } else {
        if (auto ms_state = pipeline_state->MultisampleState()) {
            return ms_state->alphaToCoverageEnable == VK_TRUE;
        }
    }
    return false;
}

bool LastBound::IsAlphaToOneEnable() const {
    if (IsDynamic(CB_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT)) {
            return cb_state.dynamic_state_value.alpha_to_one_enable;
        }
    } else {
        if (auto ms_state = pipeline_state->MultisampleState()) {
            return ms_state->alphaToOneEnable == VK_TRUE;
        }
    }
    return false;
}

VkCoverageModulationModeNV LastBound::GetCoverageModulationMode() const {
    if (IsDynamic(CB_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_COVERAGE_MODULATION_MODE_NV)) {
            return cb_state.dynamic_state_value.coverage_modulation_mode;
        }
    } else {
        if (auto ms_state = pipeline_state->MultisampleState()) {
            if (const auto *coverage_modulation_state =
                    vku::FindStructInPNextChain<VkPipelineCoverageModulationStateCreateInfoNV>(ms_state->pNext)) {
                return coverage_modulation_state->coverageModulationMode;
            }
        }
    }
    return VK_COVERAGE_MODULATION_MODE_NONE_NV;
}

uint32_t LastBound::GetViewportSwizzleCount() const {
    if (IsDynamic(CB_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_VIEWPORT_SWIZZLE_NV)) {
            return cb_state.dynamic_state_value.viewport_swizzle_count;
        }
    } else {
        if (auto viewport_state = pipeline_state->ViewportState()) {
            if (const auto *viewport_swizzle_state =
                    vku::FindStructInPNextChain<VkPipelineViewportSwizzleStateCreateInfoNV>(viewport_state->pNext)) {
                return viewport_swizzle_state->viewportCount;
            }
        }
    }
    return 0;
}

VkPolygonMode LastBound::GetPolygonMode() const {
    if (IsDynamic(CB_DYNAMIC_STATE_POLYGON_MODE_EXT)) {
        if (cb_state.IsDynamicStateSet(CB_DYNAMIC_STATE_POLYGON_MODE_EXT)) {
            return cb_state.dynamic_state_value.polygon_mode;
        }
    } else {
        if (pipeline_state->RasterizationState()) {
            return pipeline_state->RasterizationState()->polygonMode;
        }
    }
    return VK_POLYGON_MODE_MAX_ENUM;
}

// vkspec.html#drawing-vertex-input-assembler-topology
// When calling, we don't have to worry about Mesh shading because either VUs like 07065/07066 prevent these dynamic state being set
// and the VkPipelineInputAssemblyStateCreateInfo is ignored
VkPrimitiveTopology LastBound::GetVertexInputAssemblerTopology() const {
    if (IsDynamic(CB_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY)) {
        return cb_state.dynamic_state_value.primitive_topology;
    } else {
        if (auto ia_state = pipeline_state->InputAssemblyState()) {
            return ia_state->topology;
        }
    }
    return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

std::string LastBound::DescribeVertexInputAssemblerTopology() const {
    if (IsDynamic(CB_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY)) {
        return "the last call to vkCmdSetPrimitiveTopology";
    }
    return "VkPipelineInputAssemblyStateCreateInfo::topology";
}

// vkspec.html#drawing-clip-space-topology
VkPrimitiveTopology LastBound::ClipSpaceTopology() const {
    VkShaderStageFlags bound_stages = GetAllActiveBoundStages();
    const bool geom_shader_bound = (bound_stages & VK_SHADER_STAGE_GEOMETRY_BIT) != 0;
    const bool tesc_shader_bound = (bound_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) != 0;
    const bool tese_shader_bound = (bound_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) != 0;
    const bool mesh_shader_bound = (bound_stages & VK_SHADER_STAGE_MESH_BIT_EXT) != 0;
    if (!geom_shader_bound && !tesc_shader_bound && !tese_shader_bound && !mesh_shader_bound) {
        return GetVertexInputAssemblerTopology();  // vertex is the last pre-rasterization stage
    }

    if (pipeline_state) {
        if (mesh_shader_bound || geom_shader_bound) {
            // Can only have either a mesh or geometry stage, so can search both at once
            assert(!mesh_shader_bound || !geom_shader_bound);
            for (const ShaderStageState &shader_stage_state : pipeline_state->stage_states) {
                if (shader_stage_state.GetStage() == VK_SHADER_STAGE_MESH_BIT_EXT ||
                    shader_stage_state.GetStage() == VK_SHADER_STAGE_GEOMETRY_BIT) {
                    if (shader_stage_state.spirv_state && shader_stage_state.entrypoint) {
                        return shader_stage_state.entrypoint->execution_mode.GetGeometryMeshOutputTopology();
                    }
                }
            }
        } else if (tesc_shader_bound || tese_shader_bound) {
            VkPrimitiveTopology tess_output_topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
            for (const ShaderStageState &shader_stage_state : pipeline_state->stage_states) {
                const VkShaderStageFlagBits stage = shader_stage_state.GetStage();
                if (stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT || stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
                    if (shader_stage_state.spirv_state && shader_stage_state.entrypoint) {
                        // In tessellation shaders, PointMode is separate and trumps the tessellation topology.
                        // Can be found in both tessellation shaders
                        if (shader_stage_state.entrypoint->execution_mode.Has(spirv::ExecutionModeSet::point_mode_bit)) {
                            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                        } else if (stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
                            tess_output_topology =
                                shader_stage_state.entrypoint->execution_mode.GetTessellationEvalOutputTopology();
                        }
                    }
                }
            }

            return tess_output_topology;
        }
    } else {  // shader object
        if (mesh_shader_bound) {
            vvl::ShaderObject *mesh_shader = GetShaderState(ShaderObjectStage::MESH);
            if (mesh_shader && mesh_shader->entrypoint) {
                return mesh_shader->entrypoint->execution_mode.GetGeometryMeshOutputTopology();
            }
        } else if (geom_shader_bound) {
            vvl::ShaderObject *geom_shader = GetShaderState(ShaderObjectStage::GEOMETRY);
            if (geom_shader && geom_shader->entrypoint) {
                return geom_shader->entrypoint->execution_mode.GetGeometryMeshOutputTopology();
            }
        } else if (tesc_shader_bound || tese_shader_bound) {
            VkPrimitiveTopology tess_output_topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
            vvl::ShaderObject *tesc_shader = GetShaderState(ShaderObjectStage::TESSELLATION_CONTROL);
            if (tesc_shader && tesc_shader->entrypoint) {
                if (tesc_shader->entrypoint->execution_mode.Has(spirv::ExecutionModeSet::point_mode_bit)) {
                    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                }
            }

            vvl::ShaderObject *tese_shader = GetShaderState(ShaderObjectStage::TESSELLATION_EVALUATION);
            if (tese_shader && tese_shader->entrypoint) {
                if (tese_shader->entrypoint->execution_mode.Has(spirv::ExecutionModeSet::point_mode_bit)) {
                    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                } else {
                    tess_output_topology = tese_shader->entrypoint->execution_mode.GetTessellationEvalOutputTopology();
                }
            }

            return tess_output_topology;
        }
    }

    // can happen when dealing with things like VK_SHADER_CODE_TYPE_SPIRV_EXT
    return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
}

// vkspec.html#drawing-rasterization-input-topology
// The Topology can alter from Vertex/Mesh before going to rasterization
// This is the "final" topology which most VUs care about
// For additional info https://github.com/KhronosGroup/Vulkan-Guide/blob/main/chapters/primitive_topology.adoc
VkPrimitiveTopology LastBound::GetRasterizationInputTopology() const {
    VkPrimitiveTopology topology = ClipSpaceTopology();
    VkPolygonMode polygon_mode = GetPolygonMode();

    if (polygon_mode == VK_POLYGON_MODE_POINT && (IsLineTopology(topology) || IsTriangleTopology(topology))) {
        topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    } else if (polygon_mode == VK_POLYGON_MODE_LINE && IsTriangleTopology(topology)) {
        topology = TriangleToLineTopology(topology);
    }

    return topology;
}

VkShaderEXT LastBound::GetShader(ShaderObjectStage stage) const {
    if (!IsValidShaderBound(stage) || GetShaderState(stage) == nullptr) return VK_NULL_HANDLE;
    return shader_object_states[static_cast<uint32_t>(stage)]->VkHandle();
}

vvl::ShaderObject *LastBound::GetShaderState(ShaderObjectStage stage) const {
    return shader_object_states[static_cast<uint32_t>(stage)];
}

const vvl::ShaderObject *LastBound::GetShaderStateIfValid(ShaderObjectStage stage) const {
    if (!shader_object_bound[static_cast<uint32_t>(stage)]) {
        return nullptr;
    }
    return shader_object_states[static_cast<uint32_t>(stage)];
}

const vvl::ShaderObject *LastBound::GetFirstShader() const {
    if (bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
        return GetShaderStateIfValid(ShaderObjectStage::COMPUTE);
    } else if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        if (const vvl::ShaderObject *vs = GetShaderStateIfValid(ShaderObjectStage::VERTEX)) {
            return vs;
        }

        if (const vvl::ShaderObject *ms = GetShaderStateIfValid(ShaderObjectStage::MESH)) {
            return ms;
        }
    }

    return nullptr;
}

bool LastBound::HasShaderObjects() const {
    for (uint32_t i = 0; i < kShaderObjectStageCount; ++i) {
        if (GetShader(static_cast<ShaderObjectStage>(i)) != VK_NULL_HANDLE) {
            return true;
        }
    }
    return false;
}

bool LastBound::IsValidShaderBound(ShaderObjectStage stage) const { return GetShaderStateIfValid(stage) != nullptr; }

bool LastBound::IsValidShaderOrNullBound(ShaderObjectStage stage) const {
    return shader_object_bound[static_cast<uint32_t>(stage)];
}

std::vector<vvl::ShaderObject *> LastBound::GetAllBoundGraphicsShaders() {
    std::vector<vvl::ShaderObject *> shaders;

    if (IsValidShaderBound(ShaderObjectStage::VERTEX)) {
        shaders.emplace_back(shader_object_states[static_cast<uint32_t>(ShaderObjectStage::VERTEX)]);
    }
    if (IsValidShaderBound(ShaderObjectStage::TESSELLATION_CONTROL)) {
        shaders.emplace_back(shader_object_states[static_cast<uint32_t>(ShaderObjectStage::TESSELLATION_CONTROL)]);
    }
    if (IsValidShaderBound(ShaderObjectStage::TESSELLATION_EVALUATION)) {
        shaders.emplace_back(shader_object_states[static_cast<uint32_t>(ShaderObjectStage::TESSELLATION_EVALUATION)]);
    }
    if (IsValidShaderBound(ShaderObjectStage::GEOMETRY)) {
        shaders.emplace_back(shader_object_states[static_cast<uint32_t>(ShaderObjectStage::GEOMETRY)]);
    }
    if (IsValidShaderBound(ShaderObjectStage::FRAGMENT)) {
        shaders.emplace_back(shader_object_states[static_cast<uint32_t>(ShaderObjectStage::FRAGMENT)]);
    }
    if (IsValidShaderBound(ShaderObjectStage::TASK)) {
        shaders.emplace_back(shader_object_states[static_cast<uint32_t>(ShaderObjectStage::TASK)]);
    }
    if (IsValidShaderBound(ShaderObjectStage::MESH)) {
        shaders.emplace_back(shader_object_states[static_cast<uint32_t>(ShaderObjectStage::MESH)]);
    }

    return shaders;
}

bool LastBound::IsAnyGraphicsShaderBound() const {
    return IsValidShaderBound(ShaderObjectStage::VERTEX) || IsValidShaderBound(ShaderObjectStage::TESSELLATION_CONTROL) ||
           IsValidShaderBound(ShaderObjectStage::TESSELLATION_EVALUATION) || IsValidShaderBound(ShaderObjectStage::GEOMETRY) ||
           IsValidShaderBound(ShaderObjectStage::FRAGMENT) || IsValidShaderBound(ShaderObjectStage::TASK) ||
           IsValidShaderBound(ShaderObjectStage::MESH);
}

VkShaderStageFlags LastBound::GetAllActiveBoundStages() const {
    if (pipeline_state) {
        return pipeline_state->active_shaders;
    }
    // else shader object
    VkShaderStageFlags stages = 0;
    if (IsValidShaderBound(ShaderObjectStage::VERTEX)) {
        stages |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (IsValidShaderBound(ShaderObjectStage::TESSELLATION_CONTROL)) {
        stages |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    if (IsValidShaderBound(ShaderObjectStage::TESSELLATION_EVALUATION)) {
        stages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    if (IsValidShaderBound(ShaderObjectStage::GEOMETRY)) {
        stages |= VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    if (IsValidShaderBound(ShaderObjectStage::FRAGMENT)) {
        stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (IsValidShaderBound(ShaderObjectStage::COMPUTE)) {
        stages |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    if (IsValidShaderBound(ShaderObjectStage::TASK)) {
        stages |= VK_SHADER_STAGE_TASK_BIT_EXT;
    }
    if (IsValidShaderBound(ShaderObjectStage::MESH)) {
        stages |= VK_SHADER_STAGE_MESH_BIT_EXT;
    }
    return stages;
}

bool LastBound::IsBoundSetCompatible(uint32_t set, const vvl::PipelineLayout &pipeline_layout) const {
    if ((set >= ds_slots.size()) || (set >= pipeline_layout.set_compat_ids.size())) {
        return false;
    }
    return (*(ds_slots[set].compat_id_for_set) == *(pipeline_layout.set_compat_ids[set]));
}

bool LastBound::IsBoundSetCompatible(uint32_t set, const vvl::ShaderObject &shader_object_state) const {
    if ((set >= ds_slots.size()) || (set >= shader_object_state.set_compat_ids.size())) {
        return false;
    }
    return (*(ds_slots[set].compat_id_for_set) == *(shader_object_state.set_compat_ids[set]));
};

std::string LastBound::DescribeNonCompatibleSet(uint32_t set, const vvl::PipelineLayout &pipeline_layout) const {
    std::ostringstream ss;
    if (set >= ds_slots.size()) {
        ss << "The set (" << set << ") is out of bounds for the number of sets bound (" << ds_slots.size()
           << ")\nHint: Make sure the previous calls to " << String(desc_set_bound_command)
           << " has all sets bound that are needed.";
    } else if (set >= pipeline_layout.set_compat_ids.size()) {
        ss << "The set (" << set << ") is out of bounds for the number of sets in the non-compatible VkPipelineLayout ("
           << pipeline_layout.set_compat_ids.size() << ")\n";
    } else {
        return ds_slots[set].compat_id_for_set->DescribeDifference(*(pipeline_layout.set_compat_ids[set]));
    }
    return ss.str();
}

std::string LastBound::DescribeNonCompatibleSet(uint32_t set, const vvl::ShaderObject &shader_object_state) const {
    std::ostringstream ss;
    if (set >= ds_slots.size()) {
        ss << "The set (" << set << ") is out of bounds for the number of sets bound (" << ds_slots.size()
           << ")\nHint: Make sure the previous calls to " << String(desc_set_bound_command)
           << " has all sets bound that are needed.";
    } else if (set >= shader_object_state.set_compat_ids.size()) {
        ss << "The set (" << set << ") is out of bounds for the number of sets in the non-compatible VkDescriptorSetLayout ("
           << shader_object_state.set_compat_ids.size() << ")\n";
    } else {
        return ds_slots[set].compat_id_for_set->DescribeDifference(*(shader_object_state.set_compat_ids[set]));
    }
    return ss.str();
}

const spirv::EntryPoint *LastBound::GetVertexEntryPoint() const {
    if (pipeline_state) {
        for (const ShaderStageState &shader_stage_state : pipeline_state->stage_states) {
            if (shader_stage_state.GetStage() != VK_SHADER_STAGE_VERTEX_BIT) {
                continue;
            }
            return shader_stage_state.entrypoint.get();
        }
        return nullptr;
    } else if (const auto *shader_object = GetShaderState(ShaderObjectStage::VERTEX)) {
        return shader_object->entrypoint.get();
    }
    return nullptr;
}

const spirv::EntryPoint *LastBound::GetFragmentEntryPoint() const {
    if (pipeline_state && pipeline_state->fragment_shader_state) {
        return pipeline_state->fragment_shader_state->fragment_entry_point.get();
    } else if (const auto *shader_object = GetShaderState(ShaderObjectStage::FRAGMENT)) {
        return shader_object->entrypoint.get();
    }
    return nullptr;
}
