#include "ShadowPass.h"

Shadow::ShadowPass::ShadowPass(lut::VulkanContext &aContext, lut::Allocator const& aAllocator, int size, int width, int height) :
    m_size(size), m_width(width), m_height(height)
{
    createRenderPass(aContext);
    createShadowArray(aContext, aAllocator);
    createFrameBuffers(aContext);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR; 
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; 
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; 
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; 
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareOp = VK_COMPARE_OP_GREATER;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.minLod = 0.f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.mipLodBias = 0.f; 

    VkSampler sampler = VK_NULL_HANDLE; 
    if( auto const res = vkCreateSampler( aContext.device, &samplerInfo, nullptr, &sampler ); 
        VK_SUCCESS != res )
    {
        throw lut::Error( "Unable to create sampler\n" 
            "vkCreateSampler() returned %s", lut::to_string(res).c_str()
        );
    }
    shadow_sampler = lut::Sampler( aContext.device, sampler );
}

void Shadow::ShadowPass::triggerPass(int index, glm::mat4 const& transform, VkCommandBuffer const&cBuffer, const std::function<void(VkCommandBuffer const&)> &onDraw)
{
    shadow_transform_ubo->data->mat = transform;
    shadow_transform_ubo->upload(cBuffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

    // Begin render pass
    VkClearValue clearValues[1]{};
    clearValues[0].depthStencil.depth = 1.f; // clear depth as 1.0f

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = m_pass.handle;
    passInfo.framebuffer = m_fbs[index].handle;
    passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
    passInfo.renderArea.extent = {(unsigned int)m_width, (unsigned int)m_height};
    passInfo.clearValueCount = sizeof(clearValues) / sizeof(VkClearValue);  
    passInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadow_pipe.pipe.handle);
    vkCmdBindDescriptorSets(cBuffer, 
        VK_PIPELINE_BIND_POINT_GRAPHICS, 
        m_shadow_pipe.layout.handle, 0,
        1, 
        &shadow_transform_ubo->set, 
        0, nullptr
    );
    onDraw(cBuffer);

    // End the render pass
    vkCmdEndRenderPass(cBuffer);
    auto &[_, img] = shadow_imgs[index];
    lut::image_barrier(cBuffer, img.image,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, // renderpass write to it
        VK_ACCESS_SHADER_READ_BIT, // color pass will read it
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // read only
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VkImageSubresourceRange{
            VK_IMAGE_ASPECT_DEPTH_BIT,
            0, 1, 0, 1
        }
    );
}

void Shadow::ShadowPass::createRenderPass(lut::VulkanContext &aContext) 
{
    VkAttachmentDescription attachments[1]{};
    attachments[0].format = VK_FORMAT_D32_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; 
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachment{}; 
    depthAttachment.attachment = 0;
    depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpasses[1]{}; 
    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = 0; 
    subpasses[0].pColorAttachments = nullptr; 
    subpasses[0].pDepthStencilAttachment = &depthAttachment; // New!

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1; 
    passInfo.pAttachments = attachments; 
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = subpasses;
    passInfo.dependencyCount = 0;
    passInfo.pDependencies = nullptr;

    VkRenderPass rpass = VK_NULL_HANDLE; 
    if( auto const res = vkCreateRenderPass( aContext.device, &passInfo, nullptr, &rpass); 
        VK_SUCCESS != res ) {
        throw lut::Error( "Unable to create render pass\n" 
            "vkCreateRenderPass() returned %s", lut::to_string(res).c_str());
    }
    m_pass = lut::RenderPass( aContext.device, rpass );
}

void Shadow::ShadowPass::createShadowArray(lut::VulkanContext &aContext, lut::Allocator const& aAllocator) 
{
    // create attachments image views for each face on each
    for (uint32_t i = 0; i < uint32_t(m_size); i ++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D; 
        imageInfo.format = VK_FORMAT_D32_SFLOAT;
        imageInfo.extent.width = m_width;
        imageInfo.extent.height = m_height;
        imageInfo.extent.depth = 1; 
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; 
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; 

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; 
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE; 
        if( auto const res = vmaCreateImage( aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr ); 
            VK_SUCCESS != res )
        {
            throw lut::Error( "Unable to allocate screen buffer image.\n"
                "vmaCreateImage() returned %s", lut::to_string(res).c_str());
        }

        lut::Image shadow_img( aAllocator.allocator, image, allocation );

        // Create the image view
        VkImageViewCreateInfo viewInfo{}; 
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = shadow_img.image; 
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.components = VkComponentMapping{};
        viewInfo.subresourceRange = VkImageSubresourceRange{
            VK_IMAGE_ASPECT_DEPTH_BIT,
            0, 1, 0, 1};
        
        VkImageView view = VK_NULL_HANDLE;

        if( auto const res = vkCreateImageView( aContext.device, &viewInfo, nullptr, &view );
            VK_SUCCESS != res )
        {
            throw lut::Error( "Unable to create image view\n" 
                "vkCreateImageView() returned %s", lut::to_string(res).c_str() );
        }
        shadow_imgs.push_back({lut::ImageView(aContext.device, view), std::move(shadow_img)});
    }
}

void Shadow::ShadowPass::configShadowPipeLine(
        lut::VulkanContext const& aContext,
        lut::ShaderModule & vert, 
        lut::ShaderModule & frag
    )
{
    PipeLineGenerator gen;
    gen.setViewPort(float(m_width), float(m_height))
	.addDescLayout(shadow_transform_ubo->layout.handle)
	.enableBlend(false)
	.setPolyGonMode(VK_POLYGON_MODE_FILL)
	.enableDepthTest(true)
    .bindVertShader(vert)
    .bindFragShader(frag)
    .addVertexInfo(0, 0, sizeof(float) * 3, VK_FORMAT_R32G32B32_SFLOAT) //position
	.setRenderMode(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    m_shadow_pipe = gen.generate(aContext, m_pass.handle);
}
void Shadow::ShadowPass::createShadowSet(lut::VulkanContext const& aContext,
            lut::Allocator const& aAllocator,
            lut::DescriptorPool const& dPool)
{
    if (shadow_transform_ubo == nullptr) {
        shadow_transform_ubo = std::make_unique<VkUBO<UShadowTransform>>(
            aContext, aAllocator, dPool, VK_SHADER_STAGE_VERTEX_BIT
        );
        shadow_transform_ubo->data = std::make_unique<UShadowTransform>();
    }
    if (shadow_set_layout.handle == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding bindings[1]{};
        bindings[0].binding = 0; // bind texture sampler 0
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = m_size; 
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; 
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = bindings;

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        if( auto const res = vkCreateDescriptorSetLayout( 
                aContext.device, 
                &layoutInfo,
                nullptr, 
                &layout ); 
            VK_SUCCESS != res )
        {
            throw lut::Error( "Unable to create descriptor set layout\n" 
                "vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
        }
        shadow_set_layout = lut::DescriptorSetLayout( aContext.device, layout );
    }
    shadow_set = lut::alloc_desc_set(aContext, dPool.handle, shadow_set_layout.handle);

    std::vector<VkDescriptorImageInfo> img_infos(m_size);
    for (int i = 0; i < m_size; i ++) {
        // write descriptor set
        auto &[view, _] = shadow_imgs[i];
        img_infos[i].sampler = shadow_sampler.handle;
        img_infos[i].imageView =  view.handle;
        img_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    VkWriteDescriptorSet desc{};
    desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    desc.dstSet = shadow_set; 
    desc.dstBinding = 0; //
    desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    desc.descriptorCount = m_size; 
    desc.pImageInfo = img_infos.data(); 
    vkUpdateDescriptorSets( aContext.device, 1, &desc, 0, nullptr );
}

void Shadow::ShadowPass::createFrameBuffers(lut::VulkanContext &aContext) 
{
    for (int i = 0; i < m_size; i ++) {
        auto &[view, _] = shadow_imgs[i];
        VkImageView attachments[1];
        attachments[0] = view.handle;
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.flags = 0; 
        fbInfo.renderPass = m_pass.handle; 
        fbInfo.attachmentCount = 1; 
        fbInfo.pAttachments = attachments; 
        fbInfo.width = m_width;
        fbInfo.height = m_height;
        fbInfo.layers = 1; 

        VkFramebuffer fb = VK_NULL_HANDLE; 
        if( auto const res = vkCreateFramebuffer( aContext.device, &fbInfo, nullptr, &fb); 
            VK_SUCCESS != res ) {
            throw lut::Error( "Unable to create framebuffer for swap chain image %zu\n"
                "vkCreateFramebuffer() returned %s", i,lut::to_string(res).c_str());
        }
        m_fbs.emplace_back(lut::Framebuffer( aContext.device, fb ));
    }
}