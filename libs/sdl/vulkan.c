#define HL_NAME(n) sdl_vk_##n
#include <hl.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#ifdef HL_WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <excpt.h>
static HMODULE vulkan_dll = NULL;
static PFN_vkGetInstanceProcAddr get_real_vkGIPA() {
	if (!vulkan_dll) {
		vulkan_dll = LoadLibraryA("vulkan-1.dll");
	}
	if (!vulkan_dll) return NULL;
	return (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkan_dll, "vkGetInstanceProcAddr");
}
#else
static PFN_vkGetInstanceProcAddr get_real_vkGIPA() {
	return (PFN_vkGetInstanceProcAddr)(void*)SDL_Vulkan_GetVkGetInstanceProcAddr();
}
#endif

typedef struct VKContext_tag {
	VkInstance instance; VkPhysicalDevice physicalDevice; VkDevice device;
	VkQueue graphicsQueue; VkQueue presentQueue;
	uint32_t graphicsFamily; uint32_t presentFamily;
	VkSurfaceKHR surface; VkSwapchainKHR swapchain;
	VkFormat swapchainFormat; VkExtent2D swapchainExtent;
	VkImage *swapchainImages; VkImageView *swapchainImageViews;
	VkImage defaultDepthImage; VkImageView defaultDepthView; VkDeviceMemory defaultDepthMem;
	VkFramebuffer *swapchainFramebuffers; VkRenderPass renderPass;
	VkDescriptorPool descriptorPool; VkSampler defaultSampler;
	VkPipelineCache pipelineCache;
	uint32_t swapchainImageCount; uint32_t currentImageIndex;
	VkCommandPool commandPool; VkCommandBuffer *commandBuffers; uint32_t commandBufferCount;
	VkSemaphore imageAvailableSemaphore; VkSemaphore renderFinishedSemaphore; VkFence inFlightFence;
	VkSemaphore timelineSemaphore;
	int width; int height; bool swapchainDirty; bool started; bool cmdStarted;
	VkSurfaceTransformFlagBitsKHR swapchainTransform;
	uint32_t apiVersion;
	bool hasDynamicRendering;
	bool hasSynchronization2;
	bool hasDescriptorIndexing;
	bool hasTimelineSemaphore;
	bool hasPushDescriptor;
	bool hasExtendedDynamicStates;
	uint32_t maxPushConstantsSize;
	uint32_t minUBOAlignment;
	// Dynamic UBO for large push constant fallback (when size >= maxPushConstantsSize)
	VkBuffer dynamicUBO;
	VkDeviceMemory dynamicUBOMem;
	void *dynamicUBOMapped;
	int dynamicUBOSize;
	int dynamicUBOAlignment;
	int dynamicUBOOffset;
	int dynamicUBODslId;
	int dynamicUBODSId;
	uint64_t timelineValue;
	float clearR, clearG, clearB, clearA;
	SDL_Window *window;
} VKContext;

static void destroy_swapchain(VKContext *ctx);

static VkInstance vkInstance;
static VkDevice vkDevice;

typedef struct { VkBuffer buffer; VkDeviceMemory memory; void *mapped; } BufInfo;
static BufInfo *g_bufs = NULL;
static int g_bufCount = 0;
static int g_bufCap = 0;

typedef struct { VkImage image; VkDeviceMemory memory; VkImageView view; VkSampler sampler; int width; int height; int format; int mipLevels; int layerCount; VkImageLayout lastLayout; } ImgInfo;
static ImgInfo *g_imgs = NULL;
static int g_imgCount = 0;
static int g_imgCap = 0;

static int g_dslCount = 0;
static int g_dslCap = 0;
static VkDescriptorSetLayout *g_dsls = NULL;

typedef struct { VkDescriptorSet ds; } DSInfo;
static DSInfo *g_dss = NULL;
static int g_dsCount = 0;
static int g_dsCap = 0;

typedef struct { VkSampler sampler; } SamplerInfo;
static SamplerInfo *g_samplers = NULL;
static int g_samplerCount = 0;
static int g_samplerCap = 0;

typedef struct { VkImageView view; } ViewInfo;
static ViewInfo *g_views = NULL;
static int g_viewCount = 0;
static int g_viewCap = 0;

static VkQueryPool *g_queries = NULL;
static int g_queryCount = 0;
static int g_queryCap = 0;

static VkEvent *g_evts = NULL;
static int g_evtCount = 0;
static int g_evtCap = 0;

static VkDescriptorUpdateTemplate *g_tpls = NULL;
static int g_tplCount = 0;
static int g_tplCap = 0;

typedef struct { VkShaderModule module; } ShdInfo;
static ShdInfo *g_shds = NULL;
static int g_shdCount = 0;
static int g_shdCap = 0;

typedef struct { VkRenderPass renderPass; } RPInfo;
static RPInfo *g_rps = NULL;
static int g_rpCount = 0;
static int g_rpCap = 0;

typedef struct { VkFramebuffer *framebuffers; uint32_t count; } FBInfo;
static FBInfo *g_fbs = NULL;
static int g_fbCount = 0;
static int g_fbCap = 0;

typedef struct { VkPipelineLayout layout; } PlInfo;
static PlInfo *g_pls = NULL;
static int g_plCount = 0;
static int g_plCap = 0;

typedef struct { VkPipeline pipeline; } PipeInfo;
static PipeInfo *g_pipes = NULL;
static int g_pipeCount = 0;
static int g_pipeCap = 0;
static int g_viStride = 0;
 static int g_viAttrCount = 0;
 static vbyte *g_viAttrData = NULL;

HL_PRIM void HL_NAME(set_vertex_input_state)(VKContext *ctx, int stride, int count, vbyte *data) {
	printf("[VKC] set_vertex_input_state: stride=%d count=%d data=%p\n", stride, count, (void*)data);
	fflush(stdout);
	g_viStride = stride;
	g_viAttrCount = count;
	if (g_viAttrData) free(g_viAttrData);
	g_viAttrData = NULL;
	if (data && count > 0) {
		g_viAttrData = (vbyte*)malloc(count * 3 * sizeof(int));
		if (g_viAttrData) memcpy(g_viAttrData, data, count * 3 * sizeof(int));
	}
	printf("[VKC] set_vertex_input: stride=%d count=%d\n", stride, count);
}
 
 static PFN_vkGetInstanceProcAddr fp_vkGetInstanceProcAddr;
static PFN_vkGetDeviceProcAddr fp_vkGetDeviceProcAddr;
static PFN_vkGetPhysicalDeviceMemoryProperties fp_vkGetPhysicalDeviceMemoryProperties;
static PFN_vkCreateInstance fp_vkCreateInstance;
static PFN_vkDestroyInstance fp_vkDestroyInstance;
static PFN_vkEnumerateInstanceVersion fp_vkEnumerateInstanceVersion;
static PFN_vkEnumeratePhysicalDevices fp_vkEnumeratePhysicalDevices;
static PFN_vkGetPhysicalDeviceProperties fp_vkGetPhysicalDeviceProperties;
static PFN_vkGetPhysicalDeviceFeatures fp_vkGetPhysicalDeviceFeatures;
static PFN_vkGetPhysicalDeviceFeatures2 fp_vkGetPhysicalDeviceFeatures2;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties fp_vkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkCreateDevice fp_vkCreateDevice;
static PFN_vkDestroyDevice fp_vkDestroyDevice;
static PFN_vkGetDeviceQueue fp_vkGetDeviceQueue;
static PFN_vkDestroySurfaceKHR fp_vkDestroySurfaceKHR;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR fp_vkGetPhysicalDeviceSurfaceSupportKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fp_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fp_vkGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fp_vkGetPhysicalDeviceSurfacePresentModesKHR;
static PFN_vkCreateSwapchainKHR fp_vkCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR fp_vkDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR fp_vkGetSwapchainImagesKHR;
static PFN_vkAcquireNextImageKHR fp_vkAcquireNextImageKHR;
static PFN_vkQueuePresentKHR fp_vkQueuePresentKHR;
static PFN_vkCreateImageView fp_vkCreateImageView;
static PFN_vkDestroyImageView fp_vkDestroyImageView;
static PFN_vkCreateRenderPass fp_vkCreateRenderPass;
static PFN_vkDestroyRenderPass fp_vkDestroyRenderPass;
static PFN_vkCreateFramebuffer fp_vkCreateFramebuffer;
static PFN_vkDestroyFramebuffer fp_vkDestroyFramebuffer;
static PFN_vkCreateShaderModule fp_vkCreateShaderModule;
static PFN_vkDestroyShaderModule fp_vkDestroyShaderModule;
static PFN_vkCreatePipelineLayout fp_vkCreatePipelineLayout;
static PFN_vkDestroyPipelineLayout fp_vkDestroyPipelineLayout;
static PFN_vkCreateGraphicsPipelines fp_vkCreateGraphicsPipelines;
static PFN_vkCreatePipelineCache fp_vkCreatePipelineCache;
static PFN_vkDestroyPipelineCache fp_vkDestroyPipelineCache;
static PFN_vkGetPipelineCacheData fp_vkGetPipelineCacheData;
static PFN_vkDestroyPipeline fp_vkDestroyPipeline;
static PFN_vkCreateDescriptorPool fp_vkCreateDescriptorPool;
static PFN_vkDestroyDescriptorPool fp_vkDestroyDescriptorPool;
static PFN_vkCreateCommandPool fp_vkCreateCommandPool;
static PFN_vkDestroyCommandPool fp_vkDestroyCommandPool;
static PFN_vkAllocateCommandBuffers fp_vkAllocateCommandBuffers;
static PFN_vkFreeCommandBuffers fp_vkFreeCommandBuffers;
static PFN_vkBeginCommandBuffer fp_vkBeginCommandBuffer;
static PFN_vkEndCommandBuffer fp_vkEndCommandBuffer;
static PFN_vkResetCommandBuffer fp_vkResetCommandBuffer;
static PFN_vkResetCommandPool fp_vkResetCommandPool;
static PFN_vkQueueSubmit fp_vkQueueSubmit;
static PFN_vkQueueSubmit2 fp_vkQueueSubmit2;
static PFN_vkDeviceWaitIdle fp_vkDeviceWaitIdle;
static PFN_vkCreateBuffer fp_vkCreateBuffer;
static PFN_vkDestroyBuffer fp_vkDestroyBuffer;
static PFN_vkGetBufferMemoryRequirements fp_vkGetBufferMemoryRequirements;
static PFN_vkAllocateMemory fp_vkAllocateMemory;
static PFN_vkFreeMemory fp_vkFreeMemory;
static PFN_vkBindBufferMemory fp_vkBindBufferMemory;
static PFN_vkMapMemory fp_vkMapMemory;
static PFN_vkUnmapMemory fp_vkUnmapMemory;
static PFN_vkCreateSampler fp_vkCreateSampler;
static PFN_vkDestroySampler fp_vkDestroySampler;
static PFN_vkCreateSemaphore fp_vkCreateSemaphore;
static PFN_vkDestroySemaphore fp_vkDestroySemaphore;
static PFN_vkCreateFence fp_vkCreateFence;
static PFN_vkDestroyFence fp_vkDestroyFence;
static PFN_vkWaitForFences fp_vkWaitForFences;
static PFN_vkResetFences fp_vkResetFences;
static PFN_vkWaitSemaphores fp_vkWaitSemaphores;
static PFN_vkSignalSemaphore fp_vkSignalSemaphore;
static PFN_vkCmdPushDescriptorSetKHR fp_vkCmdPushDescriptorSetKHR;
static PFN_vkCmdBeginRenderPass fp_vkCmdBeginRenderPass;
static PFN_vkCmdEndRenderPass fp_vkCmdEndRenderPass;
static PFN_vkCmdBeginRendering fp_vkCmdBeginRendering;
static PFN_vkCmdEndRendering fp_vkCmdEndRendering;
static PFN_vkCmdBindPipeline fp_vkCmdBindPipeline;
static PFN_vkCmdBindVertexBuffers fp_vkCmdBindVertexBuffers;
static PFN_vkCmdBindIndexBuffer fp_vkCmdBindIndexBuffer;
static PFN_vkCmdDrawIndexed fp_vkCmdDrawIndexed;
static PFN_vkCmdDraw fp_vkCmdDraw;
static PFN_vkCmdSetViewport fp_vkCmdSetViewport;
static PFN_vkCmdSetScissor fp_vkCmdSetScissor;
static PFN_vkEnumerateInstanceExtensionProperties fp_vkEnumerateInstanceExtensionProperties;
static PFN_vkEnumerateDeviceExtensionProperties fp_vkEnumerateDeviceExtensionProperties;
static PFN_vkEnumerateInstanceLayerProperties fp_vkEnumerateInstanceLayerProperties;
static PFN_vkCreateImage fp_vkCreateImage;
static PFN_vkDestroyImage fp_vkDestroyImage;
static PFN_vkGetImageMemoryRequirements fp_vkGetImageMemoryRequirements;
static PFN_vkBindImageMemory fp_vkBindImageMemory;
static PFN_vkCmdPipelineBarrier fp_vkCmdPipelineBarrier;
static PFN_vkCmdPipelineBarrier2 fp_vkCmdPipelineBarrier2;
static PFN_vkCmdCopyBufferToImage fp_vkCmdCopyBufferToImage;
static PFN_vkCmdCopyImage fp_vkCmdCopyImage;
static PFN_vkCmdBlitImage fp_vkCmdBlitImage;
static PFN_vkCmdSetDepthBias fp_vkCmdSetDepthBias;
static PFN_vkCmdPushConstants fp_vkCmdPushConstants;
static PFN_vkAllocateDescriptorSets fp_vkAllocateDescriptorSets;
static PFN_vkUpdateDescriptorSets fp_vkUpdateDescriptorSets;
static PFN_vkCreateDescriptorSetLayout fp_vkCreateDescriptorSetLayout;
static PFN_vkDestroyDescriptorSetLayout fp_vkDestroyDescriptorSetLayout;
static PFN_vkCmdBindDescriptorSets fp_vkCmdBindDescriptorSets;
static PFN_vkCreateComputePipelines fp_vkCreateComputePipelines;
static PFN_vkCmdDispatch fp_vkCmdDispatch;
static PFN_vkCmdDrawIndexedIndirect fp_vkCmdDrawIndexedIndirect;
static PFN_vkCreateQueryPool fp_vkCreateQueryPool;
static PFN_vkDestroyQueryPool fp_vkDestroyQueryPool;
static PFN_vkCmdBeginQuery fp_vkCmdBeginQuery;
static PFN_vkCmdEndQuery fp_vkCmdEndQuery;
static PFN_vkCmdResetQueryPool fp_vkCmdResetQueryPool;
static PFN_vkGetQueryPoolResults fp_vkGetQueryPoolResults;
static PFN_vkCmdResolveImage fp_vkCmdResolveImage;
static PFN_vkCmdCopyBuffer fp_vkCmdCopyBuffer;
static PFN_vkCmdFillBuffer fp_vkCmdFillBuffer;
static PFN_vkCmdUpdateBuffer fp_vkCmdUpdateBuffer;
static PFN_vkCmdWriteTimestamp fp_vkCmdWriteTimestamp;
static PFN_vkCmdDrawIndirectCount fp_vkCmdDrawIndirectCount;
static PFN_vkGetBufferDeviceAddress fp_vkGetBufferDeviceAddress;
static PFN_vkUpdateDescriptorSetWithTemplate fp_vkUpdateDescriptorSetWithTemplate;
static PFN_vkCmdClearAttachments fp_vkCmdClearAttachments;
static PFN_vkCmdSetLineWidth fp_vkCmdSetLineWidth;
static PFN_vkCmdSetStencilReference fp_vkCmdSetStencilReference;
static PFN_vkCmdSetCullMode fp_vkCmdSetCullMode;
static PFN_vkCmdSetFrontFace fp_vkCmdSetFrontFace;
static PFN_vkCmdSetPrimitiveTopology fp_vkCmdSetPrimitiveTopology;
static PFN_vkCmdSetDepthTestEnable fp_vkCmdSetDepthTestEnable;
static PFN_vkCmdSetDepthWriteEnable fp_vkCmdSetDepthWriteEnable;
static PFN_vkCmdSetDepthCompareOp fp_vkCmdSetDepthCompareOp;
static PFN_vkCmdSetStencilTestEnable fp_vkCmdSetStencilTestEnable;
static PFN_vkCmdSetDepthBiasEnable fp_vkCmdSetDepthBiasEnable;
static PFN_vkCmdSetRasterizerDiscardEnable fp_vkCmdSetRasterizerDiscardEnable;
static PFN_vkGetDescriptorSetLayoutSupport fp_vkGetDescriptorSetLayoutSupport;
static PFN_vkResetQueryPool fp_vkResetQueryPool;
static PFN_vkCmdPushConstants2KHR fp_vkCmdPushConstants2KHR;
static PFN_vkCmdCopyImage fp_vkCmdCopyImage;
static PFN_vkCmdCopyImageToBuffer fp_vkCmdCopyImageToBuffer;
static PFN_vkCmdClearDepthStencilImage fp_vkCmdClearDepthStencilImage;
static PFN_vkCmdSetBlendConstants fp_vkCmdSetBlendConstants;
static PFN_vkCreateEvent fp_vkCreateEvent;
static PFN_vkDestroyEvent fp_vkDestroyEvent;
static PFN_vkCmdSetEvent fp_vkCmdSetEvent;
static PFN_vkCmdWaitEvents fp_vkCmdWaitEvents;
static PFN_vkFlushMappedMemoryRanges fp_vkFlushMappedMemoryRanges;
static PFN_vkInvalidateMappedMemoryRanges fp_vkInvalidateMappedMemoryRanges;
static PFN_vkBindImageMemory2 fp_vkBindImageMemory2;
static PFN_vkCmdSetPolygonModeEXT fp_vkCmdSetPolygonModeEXT;
static PFN_vkCmdSetPrimitiveRestartEnableEXT fp_vkCmdSetPrimitiveRestartEnableEXT;
static PFN_vkCmdSetRasterizationSamplesEXT fp_vkCmdSetRasterizationSamplesEXT;
static PFN_vkCmdDrawIndirectByteCountEXT fp_vkCmdDrawIndirectByteCountEXT;
static PFN_vkCmdSetLogicOpEnableEXT fp_vkCmdSetLogicOpEnableEXT;
static PFN_vkCmdSetLogicOpEXT fp_vkCmdSetLogicOpEXT;
static PFN_vkCmdSetColorBlendEnableEXT fp_vkCmdSetColorBlendEnableEXT;
static PFN_vkCmdSetColorBlendEquationEXT fp_vkCmdSetColorBlendEquationEXT;
static PFN_vkCmdSetColorWriteMaskEXT fp_vkCmdSetColorWriteMaskEXT;
static PFN_vkCmdSetDepthClampEnableEXT fp_vkCmdSetDepthClampEnableEXT;
static PFN_vkCmdSetProvokingVertexModeEXT fp_vkCmdSetProvokingVertexModeEXT;
static PFN_vkCmdSetLineRasterizationModeEXT fp_vkCmdSetLineRasterizationModeEXT;
static PFN_vkCmdSetTessellationDomainOriginEXT fp_vkCmdSetTessellationDomainOriginEXT;
static PFN_vkCmdCopyBuffer2 fp_vkCmdCopyBuffer2;
static PFN_vkCmdCopyImage2 fp_vkCmdCopyImage2;
static PFN_vkCmdBlitImage2 fp_vkCmdBlitImage2;
static PFN_vkSetDebugUtilsObjectNameEXT fp_vkSetDebugUtilsObjectNameEXT;
static PFN_vkCmdBeginDebugUtilsLabelEXT fp_vkCmdBeginDebugUtilsLabelEXT;
static PFN_vkCmdEndDebugUtilsLabelEXT fp_vkCmdEndDebugUtilsLabelEXT;
static PFN_vkCmdResetEvent fp_vkCmdResetEvent;
static PFN_vkGetEventStatus fp_vkGetEventStatus;
static PFN_vkCmdBeginConditionalRenderingEXT fp_vkCmdBeginConditionalRenderingEXT;
static PFN_vkCmdEndConditionalRenderingEXT fp_vkCmdEndConditionalRenderingEXT;
static PFN_vkCmdSetAlphaToOneEnableEXT fp_vkCmdSetAlphaToOneEnableEXT;
static PFN_vkCmdSetFragmentShadingRateKHR fp_vkCmdSetFragmentShadingRateKHR;
static PFN_vkCmdSetSampleLocationsEXT fp_vkCmdSetSampleLocationsEXT;
static PFN_vkCreateDescriptorUpdateTemplate fp_vkCreateDescriptorUpdateTemplate;
static PFN_vkDestroyDescriptorUpdateTemplate fp_vkDestroyDescriptorUpdateTemplate;

#define LDI(n) fp_##n = (PFN_##n)fp_vkGetInstanceProcAddr(vkInstance, #n)
#define LDD(n) fp_##n = (PFN_##n)fp_vkGetDeviceProcAddr(vkDevice, #n)

static int add_shd(VkShaderModule m) {
	if (g_shdCount >= g_shdCap) { g_shdCap = g_shdCap ? g_shdCap * 2 : 16; g_shds = (ShdInfo*)realloc(g_shds, g_shdCap * sizeof(ShdInfo)); }
	int id = g_shdCount++; g_shds[id].module = m;
	return id + 1;
}
static ShdInfo* get_shd(int i) { return (i > 0 && i <= g_shdCount) ? &g_shds[i - 1] : NULL; }

static int add_rpinfo(VkRenderPass rp) {
	if (g_rpCount >= g_rpCap) { g_rpCap = g_rpCap ? g_rpCap * 2 : 16; g_rps = (RPInfo*)realloc(g_rps, g_rpCap * sizeof(RPInfo)); }
	int id = g_rpCount++; g_rps[id].renderPass = rp;
	return id + 1;
}
static RPInfo* get_rpinfo(int i) { return (i > 0 && i <= g_rpCount) ? &g_rps[i - 1] : NULL; }

static int add_fb(VkFramebuffer *fbs, uint32_t count) {
	if (g_fbCount >= g_fbCap) { g_fbCap = g_fbCap ? g_fbCap * 2 : 16; g_fbs = (FBInfo*)realloc(g_fbs, g_fbCap * sizeof(FBInfo)); }
	int id = g_fbCount++; g_fbs[id].framebuffers = fbs; g_fbs[id].count = count;
	return id + 1;
}
static FBInfo* get_fb(int i) { return (i > 0 && i <= g_fbCount) ? &g_fbs[i - 1] : NULL; }

static int add_pl(VkPipelineLayout l) {
	if (g_plCount >= g_plCap) { g_plCap = g_plCap ? g_plCap * 2 : 16; g_pls = (PlInfo*)realloc(g_pls, g_plCap * sizeof(PlInfo)); }
	int id = g_plCount++; g_pls[id].layout = l;
	return id + 1;
}
static PlInfo* get_pl(int i) { return (i > 0 && i <= g_plCount) ? &g_pls[i - 1] : NULL; }

static int add_pipe(VkPipeline p) {
	if (g_pipeCount >= g_pipeCap) { g_pipeCap = g_pipeCap ? g_pipeCap * 2 : 16; g_pipes = (PipeInfo*)realloc(g_pipes, g_pipeCap * sizeof(PipeInfo)); }
	int id = g_pipeCount++; g_pipes[id].pipeline = p;
	return id + 1;
}
static PipeInfo* get_pipe(int i) { return (i > 0 && i <= g_pipeCount) ? &g_pipes[i - 1] : NULL; }

static int add_buf(VkBuffer b, VkDeviceMemory m);
static BufInfo* get_buf(int i);
static int add_img(VkImage img, VkDeviceMemory mem, VkImageView view, VkSampler sampler, int w, int h, int fmt, int mips, int layers);
static ImgInfo* get_img(int i);

static bool load_instance_functions(VkInstance inst) {
	vkInstance = inst;
	fp_vkGetInstanceProcAddr = get_real_vkGIPA();
	if (!fp_vkGetInstanceProcAddr) return false;
	LDI(vkDestroyInstance);
	LDI(vkEnumeratePhysicalDevices);
	LDI(vkGetPhysicalDeviceProperties);
	LDI(vkGetPhysicalDeviceQueueFamilyProperties);
	LDI(vkGetPhysicalDeviceMemoryProperties);
	LDI(vkDestroySurfaceKHR);
	LDI(vkGetPhysicalDeviceSurfaceSupportKHR);
	LDI(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	LDI(vkGetPhysicalDeviceSurfaceFormatsKHR);
	LDI(vkGetPhysicalDeviceSurfacePresentModesKHR);
	LDI(vkEnumerateInstanceExtensionProperties);
	LDI(vkEnumerateInstanceLayerProperties);
	LDI(vkGetPhysicalDeviceFeatures);
	LDI(vkCreateDevice);
	fp_vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)fp_vkGetInstanceProcAddr(inst, "vkEnumerateDeviceExtensionProperties");
	fp_vkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)fp_vkGetInstanceProcAddr(inst, "vkEnumerateInstanceVersion");
	fp_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)fp_vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceFeatures2");

	if (!fp_vkDestroyInstance || !fp_vkEnumeratePhysicalDevices || !fp_vkCreateDevice) return false;
	return true;
}

static bool load_device_functions(VkDevice dev) {
	vkDevice = dev;
	fp_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)fp_vkGetInstanceProcAddr(vkInstance, "vkGetDeviceProcAddr");
	if (!fp_vkGetDeviceProcAddr) return false;
	LDD(vkDestroyDevice);
	LDD(vkGetDeviceQueue);
	LDD(vkCreateSwapchainKHR);
	LDD(vkDestroySwapchainKHR);
	LDD(vkGetSwapchainImagesKHR);
	LDD(vkAcquireNextImageKHR);
	LDD(vkQueuePresentKHR);
	LDD(vkCreateImageView);
	LDD(vkDestroyImageView);
	LDD(vkCreateRenderPass);
	LDD(vkDestroyRenderPass);
	LDD(vkDestroyFramebuffer);
	LDD(vkCreateFramebuffer);
	LDD(vkCreateShaderModule);
	LDD(vkDestroyShaderModule);
	LDD(vkCreatePipelineLayout);
	LDD(vkDestroyPipelineLayout);
	LDD(vkCreateGraphicsPipelines);
	LDD(vkCreatePipelineCache);
	LDD(vkDestroyPipelineCache);
	LDD(vkGetPipelineCacheData);
	LDD(vkDestroyPipeline);
	LDD(vkCreateDescriptorPool);
	LDD(vkDestroyDescriptorPool);
	LDD(vkCreateCommandPool);
	LDD(vkDestroyCommandPool);
	LDD(vkAllocateCommandBuffers);
	LDD(vkFreeCommandBuffers);
	LDD(vkBeginCommandBuffer);
	LDD(vkEndCommandBuffer);
	LDD(vkCmdBeginRendering);
	LDD(vkCmdEndRendering);
	LDD(vkResetCommandBuffer);
	LDD(vkResetCommandPool);
	LDD(vkQueueSubmit);
	LDD(vkQueueSubmit2);
	LDD(vkDeviceWaitIdle);
	LDD(vkCreateBuffer);
	LDD(vkDestroyBuffer);
	LDD(vkGetBufferMemoryRequirements);
	LDD(vkAllocateMemory);
	LDD(vkFreeMemory);
	LDD(vkBindBufferMemory);
	LDD(vkMapMemory);
	LDD(vkUnmapMemory);
	LDD(vkCreateSampler);
	LDD(vkDestroySampler);
	LDD(vkCreateSemaphore);
	LDD(vkDestroySemaphore);
	LDD(vkCreateFence);
	LDD(vkDestroyFence);
	LDD(vkWaitForFences);
	LDD(vkResetFences);
	LDD(vkWaitSemaphores);
	LDD(vkSignalSemaphore);
	LDD(vkCmdPushDescriptorSetKHR);
	LDD(vkCmdBeginRenderPass);
	LDD(vkCmdEndRenderPass);
	LDD(vkCmdBindPipeline);
	LDD(vkCmdBindVertexBuffers);
	LDD(vkCmdBindIndexBuffer);
	LDD(vkCmdDrawIndexed);
	LDD(vkCmdDraw);
	LDD(vkCmdSetViewport);
	LDD(vkCmdSetScissor);
	LDD(vkCreateImage);
	LDD(vkDestroyImage);
	LDD(vkGetImageMemoryRequirements);
	LDD(vkBindImageMemory);
	LDD(vkCmdPipelineBarrier);
	LDD(vkCmdPipelineBarrier2);
	LDD(vkCmdCopyBufferToImage);
	LDD(vkCmdCopyImage);
	LDD(vkCmdBlitImage);
	LDD(vkCmdSetDepthBias);
	LDD(vkCmdPushConstants);
	LDD(vkAllocateDescriptorSets);
	LDD(vkUpdateDescriptorSets);
	LDD(vkCreateDescriptorSetLayout);
	LDD(vkDestroyDescriptorSetLayout);
	LDD(vkCmdBindDescriptorSets);
	LDD(vkCreateComputePipelines);
	LDD(vkCmdDispatch);
	LDD(vkCmdDrawIndexedIndirect);
	LDD(vkCreateQueryPool);
	LDD(vkDestroyQueryPool);
	LDD(vkCmdBeginQuery);
	LDD(vkCmdEndQuery);
	LDD(vkCmdResetQueryPool);
	LDD(vkGetQueryPoolResults);
	LDD(vkCmdResolveImage);
	LDD(vkCmdCopyBuffer);
	LDD(vkCmdFillBuffer);
	LDD(vkCmdUpdateBuffer);
	LDD(vkCmdWriteTimestamp);
	LDD(vkCmdDrawIndirectCount);
	LDD(vkGetBufferDeviceAddress);
	LDD(vkUpdateDescriptorSetWithTemplate);
	LDD(vkCmdClearAttachments);
	LDD(vkCmdSetLineWidth);
	LDD(vkCmdSetStencilReference);
	LDD(vkCmdSetCullMode);
	LDD(vkCmdSetFrontFace);
	LDD(vkCmdSetPrimitiveTopology);
	LDD(vkCmdSetDepthTestEnable);
	LDD(vkCmdSetDepthWriteEnable);
	LDD(vkCmdSetDepthCompareOp);
	LDD(vkCmdSetStencilTestEnable);
	LDD(vkCmdSetDepthBiasEnable);
	LDD(vkCmdSetRasterizerDiscardEnable);
	LDD(vkGetDescriptorSetLayoutSupport);
	LDD(vkResetQueryPool);
	LDD(vkCmdPushConstants2KHR);
	LDD(vkCmdCopyImage);
	LDD(vkCmdCopyImageToBuffer);
	LDD(vkCmdClearDepthStencilImage);
	LDD(vkCmdSetBlendConstants);
	LDD(vkCreateEvent);
	LDD(vkDestroyEvent);
	LDD(vkCmdSetEvent);
	LDD(vkCmdWaitEvents);
	LDD(vkFlushMappedMemoryRanges);
	LDD(vkInvalidateMappedMemoryRanges);
	LDD(vkBindImageMemory2);
	LDD(vkCmdSetPolygonModeEXT);
	LDD(vkCmdSetPrimitiveRestartEnableEXT);
	LDD(vkCmdSetRasterizationSamplesEXT);
	LDD(vkCmdDrawIndirectByteCountEXT);
	LDD(vkCmdSetLogicOpEnableEXT);
	LDD(vkCmdSetLogicOpEXT);
	LDD(vkCmdSetColorBlendEnableEXT);
	LDD(vkCmdSetColorBlendEquationEXT);
	LDD(vkCmdSetColorWriteMaskEXT);
	LDD(vkCmdSetDepthClampEnableEXT);
	LDD(vkCmdSetProvokingVertexModeEXT);
	LDD(vkCmdSetLineRasterizationModeEXT);
	LDD(vkCmdSetTessellationDomainOriginEXT);
	LDD(vkCmdCopyBuffer2);
	LDD(vkCmdCopyImage2);
	LDD(vkCmdBlitImage2);
	LDD(vkSetDebugUtilsObjectNameEXT);
	LDD(vkCmdBeginDebugUtilsLabelEXT);
	LDD(vkCmdEndDebugUtilsLabelEXT);
	LDD(vkCmdResetEvent);
	LDD(vkGetEventStatus);
	LDD(vkCmdBeginConditionalRenderingEXT);
	LDD(vkCmdEndConditionalRenderingEXT);
	LDD(vkCmdSetAlphaToOneEnableEXT);
	LDD(vkCmdSetFragmentShadingRateKHR);
	LDD(vkCmdSetSampleLocationsEXT);
	LDD(vkCreateDescriptorUpdateTemplate);
	LDD(vkDestroyDescriptorUpdateTemplate);
	if (!fp_vkDestroyDevice || !fp_vkCreateSwapchainKHR || !fp_vkCreateCommandPool) return false;
	return true;
}

static uint32_t find_mem_type(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props) {
	VkPhysicalDeviceMemoryProperties mp;
	fp_vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((filter & (1 << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
	return UINT32_MAX;
}

static bool vk_has_ext(const char **avail, uint32_t ac, const char *name) {
	for (uint32_t i = 0; i < ac; i++) if (strcmp(avail[i], name) == 0) return true;
	return false;
}

HL_PRIM VKContext *HL_NAME(create_context)(SDL_Window *window, bool debug) {
	unsigned int ec = 0;
	const char * const *en = SDL_Vulkan_GetInstanceExtensions(&ec);
	if (!en || ec == 0) { hl_error("SDL_Vulkan_GetInstanceExtensions failed"); return NULL; }

	const char **exts = (const char **)malloc((ec + 2) * sizeof(const char *));
	unsigned int ec2 = 0;
	for (unsigned int i = 0; i < ec; i++) {
		if (!debug && (strstr(en[i], "debug") || strstr(en[i], "validation"))) continue;
		exts[ec2++] = en[i];
	}
	ec = ec2;

	PFN_vkGetInstanceProcAddr vkGIPA = get_real_vkGIPA();
	if (!vkGIPA) { free(exts); hl_error("No vkGetInstanceProcAddr"); return NULL; }

	const char *validationLayers[1];
	unsigned int vlCount = 0;
	if (debug) {
		PFN_vkEnumerateInstanceLayerProperties localEnumerateLayers = (PFN_vkEnumerateInstanceLayerProperties)vkGIPA(NULL, "vkEnumerateInstanceLayerProperties");
		if (localEnumerateLayers) {
			uint32_t layerCount;
			localEnumerateLayers(&layerCount, NULL);
			if (layerCount > 0) {
				VkLayerProperties *layers = (VkLayerProperties *)malloc(layerCount * sizeof(VkLayerProperties));
				localEnumerateLayers(&layerCount, layers);
				for (uint32_t i = 0; i < layerCount; i++) {
					if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
						validationLayers[0] = "VK_LAYER_KHRONOS_validation";
						vlCount = 1;
						break;
					}
				}
				free(layers);
			}
		}
	}

	PFN_vkCreateInstance localCreateInstance = (PFN_vkCreateInstance)vkGIPA(NULL, "vkCreateInstance");
	if (!localCreateInstance) { free(exts); hl_error("No vkCreateInstance"); return NULL; }

	VkApplicationInfo ai = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Heaps",
		.applicationVersion = VK_MAKE_API_VERSION(0, 2, 1, 0),
		.pEngineName = "Heaps",
		.engineVersion = VK_MAKE_API_VERSION(0, 2, 1, 0),
		.apiVersion = VK_API_VERSION_1_0
	};

	VkInstanceCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &ai,
		.enabledExtensionCount = ec,
		.ppEnabledExtensionNames = exts,
		.enabledLayerCount = vlCount,
		.ppEnabledLayerNames = vlCount > 0 ? validationLayers : NULL,
	};

	VKContext *ctx = (VKContext *)calloc(1, sizeof(VKContext));
	ctx->window = window;

	uint32_t attemptVersions[] = {
#ifdef VK_API_VERSION_1_4
		VK_API_VERSION_1_4,
#endif
		VK_API_VERSION_1_3, VK_API_VERSION_1_2, VK_API_VERSION_1_1, VK_API_VERSION_1_0};
	int numAttempts = sizeof(attemptVersions) / sizeof(attemptVersions[0]);
	VkResult res = VK_ERROR_INITIALIZATION_FAILED;
	uint32_t selectedVersion = VK_API_VERSION_1_0;
	for (int vi = 0; vi < numAttempts; vi++) {
		ai.apiVersion = attemptVersions[vi];
		res = localCreateInstance(&ci, NULL, &ctx->instance);
		if (res == VK_SUCCESS) { selectedVersion = attemptVersions[vi]; break; }
	}
	free(exts);
	if (res != VK_SUCCESS) { free(ctx); hl_error("VkInstance failed"); return NULL; }
	ctx->apiVersion = selectedVersion;
	printf("[VK] Instance created with API %d.%d.%d\n",
		VK_API_VERSION_MAJOR(selectedVersion),
		VK_API_VERSION_MINOR(selectedVersion),
		VK_API_VERSION_PATCH(selectedVersion));

	if (!load_instance_functions(ctx->instance)) { fp_vkDestroyInstance(ctx->instance, NULL); free(ctx); hl_error("Load instance funcs failed"); return NULL; }

	uint32_t dc = 0;
	fp_vkEnumeratePhysicalDevices(ctx->instance, &dc, NULL);
	if (dc == 0) { fp_vkDestroyInstance(ctx->instance, NULL); free(ctx); hl_error("No GPU"); return NULL; }
	VkPhysicalDevice *devs = (VkPhysicalDevice *)malloc(dc * sizeof(VkPhysicalDevice));
	fp_vkEnumeratePhysicalDevices(ctx->instance, &dc, devs);
	ctx->physicalDevice = devs[0];
	for (uint32_t i = 0; i < dc; i++) {
		VkPhysicalDeviceProperties p;
		fp_vkGetPhysicalDeviceProperties(devs[i], &p);
		if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { ctx->physicalDevice = devs[i]; break; }
	}
	free(devs);

	{
		VkPhysicalDeviceProperties dp;
		fp_vkGetPhysicalDeviceProperties(ctx->physicalDevice, &dp);
		ctx->maxPushConstantsSize = dp.limits.maxPushConstantsSize;
		printf("[VK] GPU: %s | Driver: %d.%d.%d | Device API: %d.%d.%d\n",
			dp.deviceName,
			VK_API_VERSION_MAJOR(dp.driverVersion),
			VK_API_VERSION_MINOR(dp.driverVersion),
			VK_API_VERSION_PATCH(dp.driverVersion),
			VK_API_VERSION_MAJOR(dp.apiVersion),
			VK_API_VERSION_MINOR(dp.apiVersion),
			VK_API_VERSION_PATCH(dp.apiVersion));
	}

	if (!SDL_Vulkan_CreateSurface(window, ctx->instance, NULL, &ctx->surface)) {
		fp_vkDestroyInstance(ctx->instance, NULL); free(ctx); hl_error("Surface failed"); return NULL;
	}

	uint32_t qc = 0;
	fp_vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &qc, NULL);
	VkQueueFamilyProperties *qfs = (VkQueueFamilyProperties *)malloc(qc * sizeof(VkQueueFamilyProperties));
	fp_vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &qc, qfs);
	ctx->graphicsFamily = UINT32_MAX; ctx->presentFamily = UINT32_MAX;
	for (uint32_t i = 0; i < qc; i++) {
		if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ctx->graphicsFamily = i;
		VkBool32 ps = VK_FALSE;
		fp_vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physicalDevice, i, ctx->surface, &ps);
		if (ps) ctx->presentFamily = i;
		if (ctx->graphicsFamily != UINT32_MAX && ctx->presentFamily != UINT32_MAX) break;
	}
	free(qfs);
	if (ctx->graphicsFamily == UINT32_MAX) { fp_vkDestroyInstance(ctx->instance, NULL); free(ctx); hl_error("No gfx queue"); return NULL; }
	if (ctx->presentFamily == UINT32_MAX) ctx->presentFamily = ctx->graphicsFamily;

	float qp = 1.0f;
	uint32_t uf[] = {ctx->graphicsFamily, ctx->presentFamily};
	uint32_t uc = (ctx->graphicsFamily == ctx->presentFamily) ? 1 : 2;
	VkDeviceQueueCreateInfo qci[2];
	memset(&qci[0], 0, sizeof(qci));
	for (uint32_t i = 0; i < uc; i++) {
		qci[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		qci[i].queueFamilyIndex = uf[i];
		qci[i].queueCount = 1;
		qci[i].pQueuePriorities = &qp;
	}

	VkPhysicalDeviceFeatures featSupport = {0};
	fp_vkGetPhysicalDeviceFeatures(ctx->physicalDevice, &featSupport);
	VkPhysicalDeviceFeatures feats = {0};
	if (featSupport.samplerAnisotropy) feats.samplerAnisotropy = VK_TRUE;
	if (featSupport.fillModeNonSolid) feats.fillModeNonSolid = VK_TRUE;
	if (featSupport.depthClamp) feats.depthClamp = VK_TRUE;
	if (featSupport.pipelineStatisticsQuery) feats.pipelineStatisticsQuery = VK_TRUE;

	ctx->hasDynamicRendering = ctx->apiVersion >= VK_API_VERSION_1_3;
	ctx->hasSynchronization2 = ctx->apiVersion >= VK_API_VERSION_1_3;
	ctx->hasDescriptorIndexing = ctx->apiVersion >= VK_API_VERSION_1_2;
	ctx->hasTimelineSemaphore = ctx->apiVersion >= VK_API_VERSION_1_2;
	ctx->hasPushDescriptor = false;
	ctx->hasExtendedDynamicStates = false;
	const char *dynRenderExt = NULL;
	const char *sync2Ext = NULL;
	const char *descIdxExt = NULL;
	const char *pushDescExt = NULL;

	if (!ctx->hasDynamicRendering || !ctx->hasSynchronization2 || !ctx->hasDescriptorIndexing || true) {
		uint32_t extCount = 0;
		fp_vkEnumerateDeviceExtensionProperties(ctx->physicalDevice, NULL, &extCount, NULL);
		VkExtensionProperties *extProps = (VkExtensionProperties *)malloc(extCount * sizeof(VkExtensionProperties));
		fp_vkEnumerateDeviceExtensionProperties(ctx->physicalDevice, NULL, &extCount, extProps);
		for (uint32_t i = 0; i < extCount; i++) {
			if (!ctx->hasDynamicRendering && strcmp(extProps[i].extensionName, "VK_KHR_dynamic_rendering") == 0) {
				ctx->hasDynamicRendering = true;
				dynRenderExt = "VK_KHR_dynamic_rendering";
			}
			if (!ctx->hasSynchronization2 && strcmp(extProps[i].extensionName, "VK_KHR_synchronization2") == 0) {
				ctx->hasSynchronization2 = true;
				sync2Ext = "VK_KHR_synchronization2";
			}
			if (!ctx->hasDescriptorIndexing && strcmp(extProps[i].extensionName, "VK_KHR_descriptor_indexing") == 0) {
				ctx->hasDescriptorIndexing = true;
				descIdxExt = "VK_KHR_descriptor_indexing";
			}
			if (strcmp(extProps[i].extensionName, "VK_KHR_push_descriptor") == 0) {
				ctx->hasPushDescriptor = true;
				pushDescExt = "VK_KHR_push_descriptor";
			}
		}
		free(extProps);
	}

	int devExtCount = 1;
	const char *devexts[6];
	devexts[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if (dynRenderExt) devexts[devExtCount++] = dynRenderExt;
	if (sync2Ext) devexts[devExtCount++] = sync2Ext;
	if (descIdxExt) devexts[devExtCount++] = descIdxExt;
	if (pushDescExt) devexts[devExtCount++] = pushDescExt;

	VkPhysicalDeviceDynamicRenderingFeatures drf = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
		.dynamicRendering = ctx->hasDynamicRendering ? VK_TRUE : VK_FALSE,
	};
	VkPhysicalDeviceSynchronization2Features s2f = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
		.synchronization2 = ctx->hasSynchronization2 ? VK_TRUE : VK_FALSE,
	};
	VkPhysicalDeviceDescriptorIndexingFeatures dif = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
		.descriptorBindingSampledImageUpdateAfterBind = ctx->hasDescriptorIndexing ? VK_TRUE : VK_FALSE,
		.descriptorBindingStorageBufferUpdateAfterBind = ctx->hasDescriptorIndexing ? VK_TRUE : VK_FALSE,
	};
	VkPhysicalDeviceTimelineSemaphoreFeatures tsf = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
		.timelineSemaphore = ctx->hasTimelineSemaphore ? VK_TRUE : VK_FALSE,
	};

	void *pNext = NULL;
	if (ctx->hasTimelineSemaphore) { tsf.pNext = pNext; pNext = &tsf; }
	if (ctx->hasDescriptorIndexing) { dif.pNext = pNext; pNext = &dif; }
	if (ctx->hasSynchronization2) { s2f.pNext = pNext; pNext = &s2f; }
	if (ctx->hasDynamicRendering) { drf.pNext = pNext; pNext = &drf; }

	VkDeviceCreateInfo dci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = pNext,
		.queueCreateInfoCount = uc,
		.pQueueCreateInfos = qci,
		.enabledExtensionCount = devExtCount,
		.ppEnabledExtensionNames = devexts,
		.pEnabledFeatures = &feats,
	};
	res = fp_vkCreateDevice(ctx->physicalDevice, &dci, NULL, &ctx->device);
	if (res != VK_SUCCESS) { fp_vkDestroyInstance(ctx->instance, NULL); free(ctx); hl_error("Device failed"); return NULL; }

	if (!load_device_functions(ctx->device)) {
		fp_vkDestroyDevice(ctx->device, NULL);
		fp_vkDestroyInstance(ctx->instance, NULL);
		free(ctx); hl_error("Load device funcs failed"); return NULL;
	}

	ctx->hasExtendedDynamicStates = fp_vkCmdSetDepthTestEnable != NULL && fp_vkCmdSetCullMode != NULL;

	printf("[VK] Dynamic rendering: %s | Sync2: %s | DescIndex: %s | PushDesc: %s | ExtDyn: %s\n",
		ctx->hasDynamicRendering ? "YES" : "NO",
		ctx->hasSynchronization2 ? "YES" : "NO",
		ctx->hasDescriptorIndexing ? "YES" : "NO",
		ctx->hasPushDescriptor ? "YES" : "NO",
		ctx->hasExtendedDynamicStates ? "YES" : "NO");

	{
		ctx->pipelineCache = VK_NULL_HANDLE;
		FILE *f = fopen("pipelines.cache", "rb");
		void *cacheData = NULL;
		size_t cacheSize = 0;
		if (f) {
			fseek(f, 0, SEEK_END);
			cacheSize = (size_t)ftell(f);
			fseek(f, 0, SEEK_SET);
			if (cacheSize > 0) {
				cacheData = malloc(cacheSize);
				if (cacheData) fread(cacheData, 1, cacheSize, f);
			}
			fclose(f);
		}
		VkPipelineCacheCreateInfo pcci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
		if (cacheData && cacheSize > 0) {
			pcci.initialDataSize = cacheSize;
			pcci.pInitialData = cacheData;
		}
		fp_vkCreatePipelineCache(ctx->device, &pcci, NULL, &ctx->pipelineCache);
		free(cacheData);
	}

	fp_vkGetDeviceQueue(ctx->device, ctx->graphicsFamily, 0, &ctx->graphicsQueue);
	fp_vkGetDeviceQueue(ctx->device, ctx->presentFamily, 0, &ctx->presentQueue);

	VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	fp_vkCreateSemaphore(ctx->device, &si, NULL, &ctx->imageAvailableSemaphore);
	fp_vkCreateSemaphore(ctx->device, &si, NULL, &ctx->renderFinishedSemaphore);

	if (ctx->hasTimelineSemaphore) {
		VkSemaphoreTypeCreateInfo sti = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue = 0,
		};
		si.pNext = &sti;
		fp_vkCreateSemaphore(ctx->device, &si, NULL, &ctx->timelineSemaphore);
		ctx->timelineValue = 0;
	}

	VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
	fp_vkCreateFence(ctx->device, &fi, NULL, &ctx->inFlightFence);

	VkSamplerCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.minLod = 0.0f, .maxLod = VK_LOD_CLAMP_NONE,
	};
	fp_vkCreateSampler(ctx->device, &sci, NULL, &ctx->defaultSampler);

	// Initialize dynamic UBO for large push constant fallback
	{
		VkPhysicalDeviceProperties dp;
		fp_vkGetPhysicalDeviceProperties(ctx->physicalDevice, &dp);
		ctx->minUBOAlignment = (uint32_t)dp.limits.minUniformBufferOffsetAlignment;
		ctx->dynamicUBOSize = 256 * 1024; // 256KB ring buffer
		ctx->dynamicUBOAlignment = (int)ctx->minUBOAlignment;
		ctx->dynamicUBOOffset = 0;
		ctx->dynamicUBODslId = -1;
		ctx->dynamicUBODSId = -1;

		VkBufferCreateInfo uboBI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0,
			(VkDeviceSize)ctx->dynamicUBOSize,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_SHARING_MODE_EXCLUSIVE };
		if (fp_vkCreateBuffer(ctx->device, &uboBI, NULL, &ctx->dynamicUBO) == VK_SUCCESS) {
			VkMemoryRequirements mr;
			fp_vkGetBufferMemoryRequirements(ctx->device, ctx->dynamicUBO, &mr);
			VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL,
				mr.size,
				find_mem_type(ctx->physicalDevice, mr.memoryTypeBits,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
			if (fp_vkAllocateMemory(ctx->device, &ai, NULL, &ctx->dynamicUBOMem) == VK_SUCCESS) {
				fp_vkBindBufferMemory(ctx->device, ctx->dynamicUBO, ctx->dynamicUBOMem, 0);
				fp_vkMapMemory(ctx->device, ctx->dynamicUBOMem, 0, (VkDeviceSize)ctx->dynamicUBOSize, 0, &ctx->dynamicUBOMapped);

				// Create DSL for dynamic UBO (UNIFORM_BUFFER_DYNAMIC at set 3)
				VkDescriptorSetLayoutBinding uboBind = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT };
				VkDescriptorSetLayoutCreateInfo dslCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &uboBind };
				VkDescriptorSetLayout uboDSL;
				if (fp_vkCreateDescriptorSetLayout(ctx->device, &dslCI, NULL, &uboDSL) == VK_SUCCESS) {
					if (g_dslCount >= g_dslCap) { g_dslCap = g_dslCap ? g_dslCap * 2 : 16; g_dsls = (VkDescriptorSetLayout*)realloc(g_dsls, g_dslCap * sizeof(VkDescriptorSetLayout)); }
					ctx->dynamicUBODslId = g_dslCount++;
					g_dsls[ctx->dynamicUBODslId] = uboDSL;

					// Allocate descriptor set
					if (ctx->descriptorPool) {
						VkDescriptorSetAllocateInfo dsAI = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
							NULL, ctx->descriptorPool, 1, &uboDSL };
						VkDescriptorSet ds;
						if (fp_vkAllocateDescriptorSets(ctx->device, &dsAI, &ds) == VK_SUCCESS) {
							if (g_dsCount >= g_dsCap) { g_dsCap = g_dsCap ? g_dsCap * 2 : 64; g_dss = (DSInfo*)realloc(g_dss, g_dsCap * sizeof(DSInfo)); }
							ctx->dynamicUBODSId = g_dsCount++;
							g_dss[ctx->dynamicUBODSId].ds = ds;

							// Update descriptor to point to the whole buffer
							VkDescriptorBufferInfo binfo = { ctx->dynamicUBO, 0, VK_WHOLE_SIZE };
							VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, NULL, &binfo, NULL };
							fp_vkUpdateDescriptorSets(ctx->device, 1, &wds, 0, NULL);
						}
					}
				}
			}
		}
		printf("[VK] Dynamic UBO: %d KB (align=%d, maxPC=%u)\n", ctx->dynamicUBOSize / 1024, ctx->dynamicUBOAlignment, ctx->maxPushConstantsSize);
	}

	return ctx;
}

HL_PRIM void HL_NAME(wait_idle)(VKContext *ctx) {
	if (ctx && ctx->device) fp_vkDeviceWaitIdle(ctx->device);
}

HL_PRIM void HL_NAME(destroy_context)(VKContext *ctx) {
	if (!ctx) return;
	if (ctx->device) {
		fp_vkDeviceWaitIdle(ctx->device);
		if (ctx->pipelineCache && fp_vkGetPipelineCacheData) {
			size_t dataSize = 0;
			if (fp_vkGetPipelineCacheData(ctx->device, ctx->pipelineCache, &dataSize, NULL) == VK_SUCCESS && dataSize > 0) {
				void *data = malloc(dataSize);
				if (data) {
					fp_vkGetPipelineCacheData(ctx->device, ctx->pipelineCache, &dataSize, data);
					FILE *f = fopen("pipelines.cache", "wb");
					if (f) { fwrite(data, 1, dataSize, f); fclose(f); }
					free(data);
				}
			}
			fp_vkDestroyPipelineCache(ctx->device, ctx->pipelineCache, NULL);
		}
		if (ctx->defaultSampler) fp_vkDestroySampler(ctx->device, ctx->defaultSampler, NULL);
		if (ctx->inFlightFence) fp_vkDestroyFence(ctx->device, ctx->inFlightFence, NULL);
		if (ctx->imageAvailableSemaphore) fp_vkDestroySemaphore(ctx->device, ctx->imageAvailableSemaphore, NULL);
		if (ctx->renderFinishedSemaphore) fp_vkDestroySemaphore(ctx->device, ctx->renderFinishedSemaphore, NULL);
		if (ctx->timelineSemaphore) fp_vkDestroySemaphore(ctx->device, ctx->timelineSemaphore, NULL);
		if (ctx->commandPool && ctx->commandBuffers) {
			fp_vkFreeCommandBuffers(ctx->device, ctx->commandPool, ctx->commandBufferCount, ctx->commandBuffers);
			free(ctx->commandBuffers);
		}
		if (ctx->commandPool) fp_vkDestroyCommandPool(ctx->device, ctx->commandPool, NULL);
		if (ctx->descriptorPool) fp_vkDestroyDescriptorPool(ctx->device, ctx->descriptorPool, NULL);
		if (ctx->dynamicUBOMapped) fp_vkUnmapMemory(ctx->device, ctx->dynamicUBOMem);
		if (ctx->dynamicUBO) fp_vkDestroyBuffer(ctx->device, ctx->dynamicUBO, NULL);
		if (ctx->dynamicUBOMem) fp_vkFreeMemory(ctx->device, ctx->dynamicUBOMem, NULL);
		ctx->dynamicUBO = VK_NULL_HANDLE;
		ctx->dynamicUBOMem = VK_NULL_HANDLE;
		ctx->dynamicUBOMapped = NULL;
		for (int i = 0; i < g_imgCount; i++) {
			if (g_imgs[i].view != VK_NULL_HANDLE) fp_vkDestroyImageView(ctx->device, g_imgs[i].view, NULL);
			if (g_imgs[i].sampler != VK_NULL_HANDLE) fp_vkDestroySampler(ctx->device, g_imgs[i].sampler, NULL);
			if (g_imgs[i].image != VK_NULL_HANDLE) fp_vkDestroyImage(ctx->device, g_imgs[i].image, NULL);
			if (g_imgs[i].memory != VK_NULL_HANDLE) fp_vkFreeMemory(ctx->device, g_imgs[i].memory, NULL);
		}
		free(g_imgs); g_imgs = NULL; g_imgCount = g_imgCap = 0;
		for (int i = 0; i < g_samplerCount; i++)
			if (g_samplers[i].sampler != VK_NULL_HANDLE) fp_vkDestroySampler(ctx->device, g_samplers[i].sampler, NULL);
		free(g_samplers); g_samplers = NULL; g_samplerCount = g_samplerCap = 0;
		for (int i = 0; i < g_bufCount; i++) {
			if (g_bufs[i].mapped) fp_vkUnmapMemory(ctx->device, g_bufs[i].memory);
			if (g_bufs[i].buffer) fp_vkDestroyBuffer(ctx->device, g_bufs[i].buffer, NULL);
			if (g_bufs[i].memory) fp_vkFreeMemory(ctx->device, g_bufs[i].memory, NULL);
		}
		free(g_bufs); g_bufs = NULL; g_bufCount = g_bufCap = 0;
		for (int i = 0; i < g_shdCount; i++)
			if (g_shds[i].module) fp_vkDestroyShaderModule(ctx->device, g_shds[i].module, NULL);
		free(g_shds); g_shds = NULL; g_shdCount = g_shdCap = 0;
		for (int i = 0; i < g_pipeCount; i++)
			if (g_pipes[i].pipeline) fp_vkDestroyPipeline(ctx->device, g_pipes[i].pipeline, NULL);
		free(g_pipes); g_pipes = NULL; g_pipeCount = g_pipeCap = 0;
		for (int i = 0; i < g_plCount; i++)
			if (g_pls[i].layout) fp_vkDestroyPipelineLayout(ctx->device, g_pls[i].layout, NULL);
		free(g_pls); g_pls = NULL; g_plCount = g_plCap = 0;
		for (int i = 0; i < g_rpCount; i++)
			if (g_rps[i].renderPass) fp_vkDestroyRenderPass(ctx->device, g_rps[i].renderPass, NULL);
		free(g_rps); g_rps = NULL; g_rpCount = g_rpCap = 0;
		for (int i = 0; i < g_fbCount; i++) {
			if (g_fbs[i].framebuffers) {
				for (uint32_t j = 0; j < g_fbs[i].count; j++)
					if (g_fbs[i].framebuffers[j]) fp_vkDestroyFramebuffer(ctx->device, g_fbs[i].framebuffers[j], NULL);
				free(g_fbs[i].framebuffers);
			}
		}
		free(g_fbs); g_fbs = NULL; g_fbCount = g_fbCap = 0;
		for (int i = 0; i < g_dslCount; i++)
			if (g_dsls[i]) fp_vkDestroyDescriptorSetLayout(ctx->device, g_dsls[i], NULL);
		free(g_dsls); g_dsls = NULL; g_dslCount = g_dslCap = 0;
		for (int i = 0; i < g_queryCount; i++)
			if (g_queries && g_queries[i] != VK_NULL_HANDLE) fp_vkDestroyQueryPool(ctx->device, g_queries[i], NULL);
		free(g_queries); g_queries = NULL; g_queryCount = g_queryCap = 0;
		for (int i = 0; i < g_evtCount; i++)
			if (g_evts && g_evts[i] != VK_NULL_HANDLE) fp_vkDestroyEvent(ctx->device, g_evts[i], NULL);
		free(g_evts); g_evts = NULL; g_evtCount = g_evtCap = 0;
		for (int i = 0; i < g_tplCount; i++)
			if (g_tpls && g_tpls[i] != VK_NULL_HANDLE && fp_vkDestroyDescriptorUpdateTemplate)
				fp_vkDestroyDescriptorUpdateTemplate(ctx->device, g_tpls[i], NULL);
		free(g_tpls); g_tpls = NULL; g_tplCount = g_tplCap = 0;
		destroy_swapchain(ctx);
		fp_vkDestroyDevice(ctx->device, NULL);
	}
	if (ctx->surface) fp_vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
	if (ctx->instance) fp_vkDestroyInstance(ctx->instance, NULL);
	free(ctx);
}

static void destroy_swapchain(VKContext *ctx) {
	if (ctx->swapchainFramebuffers) {
		for (uint32_t i = 0; i < ctx->swapchainImageCount; i++)
			if (ctx->swapchainFramebuffers[i]) fp_vkDestroyFramebuffer(ctx->device, ctx->swapchainFramebuffers[i], NULL);
		free(ctx->swapchainFramebuffers); ctx->swapchainFramebuffers = NULL;
	}
	if (ctx->swapchainImageViews) {
		for (uint32_t i = 0; i < ctx->swapchainImageCount; i++)
			if (ctx->swapchainImageViews[i]) fp_vkDestroyImageView(ctx->device, ctx->swapchainImageViews[i], NULL);
		free(ctx->swapchainImageViews); ctx->swapchainImageViews = NULL;
	}
	free(ctx->swapchainImages); ctx->swapchainImages = NULL;
	if( ctx->defaultDepthView ) { fp_vkDestroyImageView(ctx->device, ctx->defaultDepthView, NULL); ctx->defaultDepthView = VK_NULL_HANDLE; }
	if( ctx->defaultDepthImage ) { fp_vkDestroyImage(ctx->device, ctx->defaultDepthImage, NULL); ctx->defaultDepthImage = VK_NULL_HANDLE; }
	if( ctx->defaultDepthMem ) { fp_vkFreeMemory(ctx->device, ctx->defaultDepthMem, NULL); ctx->defaultDepthMem = VK_NULL_HANDLE; }
	if (ctx->swapchain) { fp_vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL); ctx->swapchain = VK_NULL_HANDLE; }
}

static bool g_vk_vsync = true;
HL_PRIM void HL_NAME(set_vk_vsync)(bool v) { g_vk_vsync = v; }

HL_PRIM void HL_NAME(dirty_swapchain)(VKContext *ctx) {
	if (ctx) ctx->swapchainDirty = true;
}

HL_PRIM int HL_NAME(get_transform)(VKContext *ctx) {
	return ctx ? (int)ctx->swapchainTransform : 1;
}

HL_PRIM int HL_NAME(get_max_push_constants_size)(VKContext *ctx) {
	return ctx ? (int)ctx->maxPushConstantsSize : 256;
}

HL_PRIM int HL_NAME(get_min_ubo_alignment)(VKContext *ctx) {
	return ctx ? (int)ctx->minUBOAlignment : 256;
}

static bool try_recreate_surface(VKContext *ctx) {
	if (!ctx->window) return false;
	// Don't try to create surface if window is occluded/minimized (Android resume not ready yet)
	SDL_WindowFlags flags = SDL_GetWindowFlags(ctx->window);
	if ((flags & SDL_WINDOW_OCCLUDED) || (flags & SDL_WINDOW_MINIMIZED))
		return false;
	VkSurfaceKHR newSurface = VK_NULL_HANDLE;
	if (!SDL_Vulkan_CreateSurface(ctx->window, ctx->instance, NULL, &newSurface))
		return false;
	if (ctx->surface)
		fp_vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
	ctx->surface = newSurface;
	return true;
}

static bool create_swapchain(VKContext *ctx, int w, int h) {
	VkSurfaceCapabilitiesKHR caps;
	fp_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physicalDevice, ctx->surface, &caps);

	uint32_t fc = 0;
	fp_vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physicalDevice, ctx->surface, &fc, NULL);
	if (fc == 0) return false;
	VkSurfaceFormatKHR *fmts = (VkSurfaceFormatKHR *)malloc(fc * sizeof(VkSurfaceFormatKHR));
	fp_vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physicalDevice, ctx->surface, &fc, fmts);
	VkSurfaceFormatKHR sf = fmts[0];
	for (uint32_t i = 0; i < fc; i++)
		if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { sf = fmts[i]; break; }
	free(fmts);

	uint32_t pmc = 0;
	fp_vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physicalDevice, ctx->surface, &pmc, NULL);
	VkPresentModeKHR pm = g_vk_vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
	if (pmc > 0) {
		VkPresentModeKHR *pms = (VkPresentModeKHR *)malloc(pmc * sizeof(VkPresentModeKHR));
		fp_vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physicalDevice, ctx->surface, &pmc, pms);
		if (g_vk_vsync) {
#ifndef HL_MOBILE
			for (uint32_t i = 0; i < pmc; i++) { if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR) { pm = VK_PRESENT_MODE_MAILBOX_KHR; break; } }
#endif
		} else {
			bool hasImmediate = false, hasMailbox = false;
			for (uint32_t i = 0; i < pmc; i++) { if (pms[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) hasImmediate = true; if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR) hasMailbox = true; }
			if (hasImmediate) pm = VK_PRESENT_MODE_IMMEDIATE_KHR;
			else if (hasMailbox) pm = VK_PRESENT_MODE_MAILBOX_KHR;
		}
		free(pms);
	}

#ifdef HL_MOBILE
	{
		int tex = (int)caps.currentExtent.width;
		int tey = (int)caps.currentExtent.height;
		if (tex != 0xFFFFFFFF && tey != 0xFFFFFFFF) { w = tex; h = tey; }
		else { if (w == 0) w = 800; if (h == 0) h = 600; }
	}
	if (w < (int)caps.minImageExtent.width) w = caps.minImageExtent.width;
	if (w > (int)caps.maxImageExtent.width) w = caps.maxImageExtent.width;
	if (h < (int)caps.minImageExtent.height) h = caps.minImageExtent.height;
	if (h > (int)caps.maxImageExtent.height) h = caps.maxImageExtent.height;
#else
	if (caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
		caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		int tmp = w; w = h; h = tmp;
	}
	{
		int tex = (int)caps.currentExtent.width;
		int tey = (int)caps.currentExtent.height;
		if (w == 0 && tex != 0xFFFFFFFF) w = tex;
		if (h == 0 && tey != 0xFFFFFFFF) h = tey;
	}
	if (w < (int)caps.minImageExtent.width) w = caps.minImageExtent.width;
	if (w > (int)caps.maxImageExtent.width) w = caps.maxImageExtent.width;
	if (h < (int)caps.minImageExtent.height) h = caps.minImageExtent.height;
	if (h > (int)caps.maxImageExtent.height) h = caps.maxImageExtent.height;
#endif

	ctx->swapchainExtent.width = w;
	ctx->swapchainExtent.height = h;
	ctx->swapchainFormat = sf.format;
	ctx->swapchainTransform = caps.currentTransform;
	printf("[VK] create_swapchain: %dx%d preTransform=%d\n", w, h, (int)caps.currentTransform);

	uint32_t ic = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && ic > caps.maxImageCount) ic = caps.maxImageCount;

	uint32_t qfi[] = {ctx->graphicsFamily, ctx->presentFamily};
	bool conc = (ctx->graphicsFamily != ctx->presentFamily);

	VkSwapchainCreateInfoKHR sci = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = ctx->surface, .minImageCount = ic,
		.imageFormat = sf.format, .imageColorSpace = sf.colorSpace,
		.imageExtent = ctx->swapchainExtent, .imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = conc ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = conc ? 2u : 0u,
		.pQueueFamilyIndices = conc ? qfi : NULL,
		.preTransform = caps.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = pm, .clipped = VK_TRUE,
	};
	if (fp_vkCreateSwapchainKHR(ctx->device, &sci, NULL, &ctx->swapchain) != VK_SUCCESS) return false;

	fp_vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->swapchainImageCount, NULL);
	ctx->swapchainImages = (VkImage *)malloc(ctx->swapchainImageCount * sizeof(VkImage));
	ctx->swapchainImageViews = (VkImageView *)malloc(ctx->swapchainImageCount * sizeof(VkImageView));
	fp_vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->swapchainImageCount, ctx->swapchainImages);

	for (uint32_t i = 0; i < ctx->swapchainImageCount; i++) {
		VkImageViewCreateInfo vi = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = ctx->swapchainImages[i], .viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = ctx->swapchainFormat,
			.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		fp_vkCreateImageView(ctx->device, &vi, NULL, &ctx->swapchainImageViews[i]);
	}

	// Create default depth buffer
	if( ctx->defaultDepthView ) { fp_vkDestroyImageView(ctx->device, ctx->defaultDepthView, NULL); ctx->defaultDepthView = VK_NULL_HANDLE; }
	if( ctx->defaultDepthImage ) { fp_vkDestroyImage(ctx->device, ctx->defaultDepthImage, NULL); ctx->defaultDepthImage = VK_NULL_HANDLE; }
	if( ctx->defaultDepthMem ) { fp_vkFreeMemory(ctx->device, ctx->defaultDepthMem, NULL); ctx->defaultDepthMem = VK_NULL_HANDLE; }
	{
		VkImageCreateInfo dimg = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_D32_SFLOAT,
			.extent = { (uint32_t)w, (uint32_t)h, 1 }, .mipLevels = 1, .arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		};
		fp_vkCreateImage(ctx->device, &dimg, NULL, &ctx->defaultDepthImage);
		VkMemoryRequirements mr;
		fp_vkGetImageMemoryRequirements(ctx->device, ctx->defaultDepthImage, &mr);
		VkMemoryAllocateInfo mai = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = mr.size,
			.memoryTypeIndex = find_mem_type(ctx->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};
		fp_vkAllocateMemory(ctx->device, &mai, NULL, &ctx->defaultDepthMem);
		fp_vkBindImageMemory(ctx->device, ctx->defaultDepthImage, ctx->defaultDepthMem, 0);

		VkImageViewCreateInfo dvi = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = ctx->defaultDepthImage, .viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_D32_SFLOAT,
			.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
		};
		fp_vkCreateImageView(ctx->device, &dvi, NULL, &ctx->defaultDepthView);
	}

	ctx->width = w; ctx->height = h;
	return true;
}

HL_PRIM bool HL_NAME(init_swapchain)(VKContext *ctx, int w, int h) {
	if (w <= 0 || h <= 0) return false;
	destroy_swapchain(ctx);
	if (!ctx->surface) {
		try_recreate_surface(ctx);
	}
	ctx->swapchainDirty = false;
	return create_swapchain(ctx, w, h);
}

HL_PRIM bool HL_NAME(is_swapchain_dirty)(VKContext *ctx) {
	return ctx ? ctx->swapchainDirty : false;
}

HL_PRIM int HL_NAME(create_render_pass)(VKContext *ctx, bool hasDepth, int depthFormat, int colorAttachmentCount, int samples) {
	VkAttachmentDescription att[9]; int ac = 0;
	memset(att, 0, sizeof(att));
	if (colorAttachmentCount <= 0) colorAttachmentCount = 1;
	if (colorAttachmentCount > 8) colorAttachmentCount = 8;
	VkSampleCountFlagBits sc = (VkSampleCountFlagBits)samples;
	if (sc < VK_SAMPLE_COUNT_1_BIT) sc = VK_SAMPLE_COUNT_1_BIT;
	for (int i = 0; i < colorAttachmentCount; i++) {
		att[ac].format = ctx->swapchainFormat; att[ac].samples = sc;
		att[ac].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att[ac].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		att[ac].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att[ac].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		ac++;
	}
	if (hasDepth) {
		att[ac].format = depthFormat; att[ac].samples = sc;
		att[ac].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att[ac].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att[ac].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att[ac].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		ac++;
	}
	VkAttachmentReference car[8];
	for (int i = 0; i < colorAttachmentCount; i++) { car[i].attachment = i; car[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
	VkAttachmentReference dar = { (uint32_t)colorAttachmentCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	VkSubpassDescription sp = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = (uint32_t)colorAttachmentCount, .pColorAttachments = car,
		.pDepthStencilAttachment = hasDepth ? &dar : NULL,
	};
	VkSubpassDependency dep = {
		.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	};
	VkRenderPassCreateInfo rpi = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = ac, .pAttachments = att,
		.subpassCount = 1, .pSubpasses = &sp, .dependencyCount = 1, .pDependencies = &dep,
	};
	VkRenderPass rp;
	fp_vkCreateRenderPass(ctx->device, &rpi, NULL, &rp);
	return add_rpinfo(rp);
}

HL_PRIM bool HL_NAME(create_framebuffers)(VKContext *ctx, int rpId) {
	RPInfo *rpi = get_rpinfo(rpId); if (!rpi) return false;
	VkRenderPass renderPass = rpi->renderPass;
	ctx->renderPass = renderPass;
	ctx->swapchainFramebuffers = (VkFramebuffer *)malloc(ctx->swapchainImageCount * sizeof(VkFramebuffer));
	for (uint32_t i = 0; i < ctx->swapchainImageCount; i++) {
		VkImageView att[] = {ctx->swapchainImageViews[i]};
		VkFramebufferCreateInfo fi = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = renderPass, .attachmentCount = 1, .pAttachments = att,
			.width = ctx->swapchainExtent.width, .height = ctx->swapchainExtent.height, .layers = 1,
		};
		if (fp_vkCreateFramebuffer(ctx->device, &fi, NULL, &ctx->swapchainFramebuffers[i]) != VK_SUCCESS) return false;
	}
	return true;
}

HL_PRIM void HL_NAME(create_descriptor_pool)(VKContext *ctx) {
	VkDescriptorPoolSize sizes[] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256}
	};
	VkDescriptorPoolCreateInfo pi = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 256, .poolSizeCount = 3, .pPoolSizes = sizes
	};
	if (ctx->hasDescriptorIndexing)
		pi.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	fp_vkCreateDescriptorPool(ctx->device, &pi, NULL, &ctx->descriptorPool);
}

HL_PRIM bool HL_NAME(create_command_pool)(VKContext *ctx) {
	VkCommandPoolCreateInfo pi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = ctx->graphicsFamily,
	};
	return fp_vkCreateCommandPool(ctx->device, &pi, NULL, &ctx->commandPool) == VK_SUCCESS;
}

HL_PRIM bool HL_NAME(create_command_buffers)(VKContext *ctx, int count) {
	ctx->commandBufferCount = count;
	ctx->commandBuffers = (VkCommandBuffer *)malloc(count * sizeof(VkCommandBuffer));
	VkCommandBufferAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = ctx->commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = count,
	};
	return fp_vkAllocateCommandBuffers(ctx->device, &ai, ctx->commandBuffers) == VK_SUCCESS;
}

HL_PRIM bool HL_NAME(begin_frame)(VKContext *ctx) {
	printf("[VKC] begin_frame\n");
	if (ctx->swapchainDirty) { printf("[VKC] begin_frame: swapchain dirty\n"); return false; }
	if (!ctx->swapchain || !ctx->commandBuffers || ctx->commandBufferCount == 0) { printf("[VKC] begin_frame: no swapchain/cmdbufs\n"); return false; }

	if (ctx->window) {
		SDL_WindowFlags flags = SDL_GetWindowFlags(ctx->window);
		if ((flags & SDL_WINDOW_MINIMIZED) || (flags & SDL_WINDOW_HIDDEN))
			return false;
	}

	// Detect surface change (orientation or window resize)
	if (ctx->surface && ctx->swapchain) {
		VkSurfaceCapabilitiesKHR caps;
		if (fp_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physicalDevice, ctx->surface, &caps) == VK_SUCCESS) {
			if (caps.currentTransform != ctx->swapchainTransform) {
				printf("[VK] ORIENTATION CHANGE: transform %d -> %d, marking swapchain dirty\n",
					(int)ctx->swapchainTransform, (int)caps.currentTransform);
				ctx->swapchainDirty = true;
				return false;
			}
		}
	}

	fp_vkDeviceWaitIdle(ctx->device);

	fp_vkResetFences(ctx->device, 1, &ctx->inFlightFence);
	{
		VkResult r = fp_vkAcquireNextImageKHR(ctx->device, ctx->swapchain, 100000000ULL, VK_NULL_HANDLE, ctx->inFlightFence, &ctx->currentImageIndex);
		printf("[VKC] acquireNextImage: idx=%d result=%d\n", ctx->currentImageIndex, r);
		if (r == VK_TIMEOUT || r == VK_NOT_READY) { printf("[VKC] acquire timeout/notready\n"); return false; }
		if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_SURFACE_LOST_KHR || r < 0) {
			destroy_swapchain(ctx);
			ctx->swapchainDirty = true;
			return false;
		}
		if (r != VK_SUCCESS) return false;
	}
	fp_vkWaitForFences(ctx->device, 1, &ctx->inFlightFence, VK_TRUE, 100000000ULL);
	fp_vkResetFences(ctx->device, 1, &ctx->inFlightFence);

	fp_vkResetCommandBuffer(ctx->commandBuffers[0], 0);
	VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 0, NULL};
	fp_vkBeginCommandBuffer(ctx->commandBuffers[0], &bi);
	ctx->cmdStarted = true;
	ctx->started = false;
	return true;
}

// Forward declaration for cmd_image_barrier
static void cmd_image_barrier(VKContext *ctx, VkCommandBuffer cb,
	uint32_t srcStage, uint32_t dstStage, uint32_t srcAccess, uint32_t dstAccess,
	VkImageLayout oldLayout, VkImageLayout newLayout, VkImage image,
	uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount);

HL_PRIM bool HL_NAME(end_frame)(VKContext *ctx) {
	printf("[VKC] end_frame\n");
	if (!ctx->commandBuffers || ctx->commandBufferCount == 0) { printf("[VKC] end_frame: no cmdbuf\n"); return false; }

	// 1. Submit the main rendering command buffer
	VkResult sr;
	if (ctx->hasSynchronization2 && fp_vkQueueSubmit2) {
		VkCommandBufferSubmitInfo cbsi = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = ctx->commandBuffers[0],
		};
		VkSubmitInfo2 si2 = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &cbsi,
		};
		sr = fp_vkQueueSubmit2(ctx->graphicsQueue, 1, &si2, VK_NULL_HANDLE);
	} else {
		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1, .pCommandBuffers = &ctx->commandBuffers[0],
		};
		sr = fp_vkQueueSubmit(ctx->graphicsQueue, 1, &si, VK_NULL_HANDLE);
	}
	printf("[VKC] queueSubmit result=%d\n", sr);
	fp_vkDeviceWaitIdle(ctx->device);
	printf("[VKC] device idle\n");

	// 2. Present
	VkPresentInfoKHR pi = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 0, .pWaitSemaphores = NULL,
		.swapchainCount = 1, .pSwapchains = &ctx->swapchain,
		.pImageIndices = &ctx->currentImageIndex,
	};
	VkResult pr = fp_vkQueuePresentKHR(ctx->presentQueue, &pi);
	printf("[VKC] present result=%d\n", pr);
	if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) ctx->swapchainDirty = true;
	return sr == VK_SUCCESS;
}

HL_PRIM int HL_NAME(get_width)(VKContext *ctx)  { return ctx->width; }
HL_PRIM int HL_NAME(get_height)(VKContext *ctx) { return ctx->height; }

HL_PRIM int HL_NAME(get_command_buffer)(VKContext *ctx, int idx) {
	if (!ctx->commandBuffers || (uint32_t)idx >= ctx->commandBufferCount) return -1;
	return idx;
}

HL_PRIM bool HL_NAME(begin_command_buffer)(VKContext *ctx, int idx) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)idx >= ctx->commandBufferCount) return false;
	ctx->cmdStarted = true;
	return true;
}

HL_PRIM bool HL_NAME(end_command_buffer)(VKContext *ctx, int idx) {
	if (!ctx->commandBuffers || (uint32_t)idx >= ctx->commandBufferCount) return false;
	bool ok = fp_vkEndCommandBuffer(ctx->commandBuffers[idx]) == VK_SUCCESS;
	if (ok && idx == 0) ctx->cmdStarted = false;
	return ok;
}

HL_PRIM void HL_NAME(clear_color_image)(VKContext *ctx, double r, double g, double b, double a) {
	if (ctx) {
		ctx->clearR = (float)r;
		ctx->clearG = (float)g;
		ctx->clearB = (float)b;
		ctx->clearA = (float)a;
	}
}

static void cmd_image_barrier(VKContext *ctx, VkCommandBuffer cb,
	uint32_t srcStage, uint32_t dstStage, uint32_t srcAccess, uint32_t dstAccess,
	VkImageLayout oldLayout, VkImageLayout newLayout, VkImage image,
	uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount)
{
	if (ctx->hasSynchronization2 && fp_vkCmdPipelineBarrier2) {
		VkImageMemoryBarrier2 imb2 = {0};
		imb2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		imb2.srcStageMask = (VkPipelineStageFlagBits2)srcStage;
		imb2.dstStageMask = (VkPipelineStageFlagBits2)dstStage;
		imb2.srcAccessMask = (VkAccessFlagBits2)srcAccess;
		imb2.dstAccessMask = (VkAccessFlagBits2)dstAccess;
		imb2.oldLayout = oldLayout;
		imb2.newLayout = newLayout;
		imb2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imb2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imb2.image = image;
		imb2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imb2.subresourceRange.baseMipLevel = baseMip;
		imb2.subresourceRange.levelCount = mipCount;
		imb2.subresourceRange.baseArrayLayer = baseLayer;
		imb2.subresourceRange.layerCount = layerCount;
		VkDependencyInfo di = {0};
		di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		di.imageMemoryBarrierCount = 1;
		di.pImageMemoryBarriers = (VkImageMemoryBarrier2KHR*)&imb2;
		fp_vkCmdPipelineBarrier2(cb, &di);
	} else {
		VkImageMemoryBarrier imb = {0};
		imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imb.srcAccessMask = (VkAccessFlags)srcAccess;
		imb.dstAccessMask = (VkAccessFlags)dstAccess;
		imb.oldLayout = oldLayout;
		imb.newLayout = newLayout;
		imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imb.image = image;
		imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imb.subresourceRange.baseMipLevel = baseMip;
		imb.subresourceRange.levelCount = mipCount;
		imb.subresourceRange.baseArrayLayer = baseLayer;
		imb.subresourceRange.layerCount = layerCount;
		fp_vkCmdPipelineBarrier(cb,
			(VkPipelineStageFlags)srcStage, (VkPipelineStageFlags)dstStage,
			0, 0, NULL, 0, NULL, 1, &imb);
	}
}

HL_PRIM bool HL_NAME(upload_texture_data)(VKContext *ctx, int imgId, vbyte *pixels, int offset, int dataLen, int mipLevel, int side) {
	if (!ctx || imgId < 0 || imgId >= g_imgCount || !pixels || dataLen <= 0) return false;
	ImgInfo *img = &g_imgs[imgId];
	if (!img->image) return false;

	VkCommandPool cmdPool;
	VkCommandPoolCreateInfo cpi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .pNext = NULL,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = ctx->graphicsFamily
	};
	if (fp_vkCreateCommandPool(ctx->device, &cpi, NULL, &cmdPool) != VK_SUCCESS) return false;

	VkCommandBuffer cb;
	VkCommandBufferAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .pNext = NULL,
		.commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1
	};
	if (fp_vkAllocateCommandBuffers(ctx->device, &ai, &cb) != VK_SUCCESS) {
		fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
		return false;
	}

	VkCommandBufferBeginInfo bi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = NULL,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, .pInheritanceInfo = NULL
	};
	if (fp_vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) {
		fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
		return false;
	}

	VkBuffer staging;
	VkDeviceMemory stagingMem;
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = NULL, .flags = 0,
		.size = dataLen, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE, .queueFamilyIndexCount = 0, .pQueueFamilyIndices = NULL
	};
	if (fp_vkCreateBuffer(ctx->device, &bci, NULL, &staging) != VK_SUCCESS) {
		fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
		return false;
	}

	VkMemoryRequirements mr;
	fp_vkGetBufferMemoryRequirements(ctx->device, staging, &mr);
	VkMemoryAllocateInfo mai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = NULL,
		.allocationSize = mr.size,
		.memoryTypeIndex = 0
	};
	{
		VkPhysicalDeviceMemoryProperties mp;
		fp_vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &mp);
		for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
			if ((mr.memoryTypeBits & (1 << i)) &&
				(mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
				mai.memoryTypeIndex = i; break;
			}
		}
	}
	if (fp_vkAllocateMemory(ctx->device, &mai, NULL, &stagingMem) != VK_SUCCESS) {
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
		return false;
	}
	fp_vkBindBufferMemory(ctx->device, staging, stagingMem, 0);

	void *mapped;
	if (fp_vkMapMemory(ctx->device, stagingMem, 0, dataLen, 0, &mapped) != VK_SUCCESS) {
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL);
		fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
		return false;
	}
	memcpy(mapped, pixels + offset, dataLen);
	fp_vkUnmapMemory(ctx->device, stagingMem);

	cmd_image_barrier(ctx, cb, 1, 4096, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
		img->lastLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		img->image, mipLevel, 1, side, 1);

	VkBufferImageCopy region = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, side, 1 }, { 0, 0, 0 }, { img->width, img->height, 1 } };
	fp_vkCmdCopyBufferToImage(cb, staging, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	cmd_image_barrier(ctx, cb, 4096, 128, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		img->image, mipLevel, 1, side, 1);

	img->lastLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	if (fp_vkEndCommandBuffer(cb) != VK_SUCCESS) {
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL);
		fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
		return false;
	}

	VkSubmitInfo si;
	memset(&si, 0, sizeof(si));
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cb;
	if (fp_vkQueueSubmit(ctx->graphicsQueue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) {
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL);
		fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
		return false;
	}
	fp_vkDeviceWaitIdle(ctx->device);

	fp_vkDestroyBuffer(ctx->device, staging, NULL);
	fp_vkFreeMemory(ctx->device, stagingMem, NULL);
	fp_vkFreeCommandBuffers(ctx->device, cmdPool, 1, &cb);
	fp_vkDestroyCommandPool(ctx->device, cmdPool, NULL);
	return true;
}

HL_PRIM void HL_NAME(begin_render_pass)(VKContext *ctx) {
	if (!ctx) return;
	if (!ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started) return;
	if (!fp_vkCmdBeginRenderPass) return;
	if (!ctx->renderPass) return;
	if (ctx->swapchainExtent.width == 0 || ctx->swapchainExtent.height == 0) return;

	VkFramebuffer *fbs = ctx->swapchainFramebuffers;
	uint32_t idx = ctx->currentImageIndex;
	uint32_t count = ctx->swapchainImageCount;
	if (!fbs || idx >= count) return;
	VkFramebuffer fb = fbs[idx];
	if (!fb) return;

	VkClearValue cv;
	memset(&cv, 0, sizeof(cv));
	cv.color.float32[0] = ctx->clearR;
	cv.color.float32[1] = ctx->clearG;
	cv.color.float32[2] = ctx->clearB;
	cv.color.float32[3] = ctx->clearA;

	VkRenderPassBeginInfo rbi;
	memset(&rbi, 0, sizeof(rbi));
	rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rbi.pNext = NULL;
	rbi.renderPass = ctx->renderPass;
	rbi.framebuffer = fb;
	rbi.renderArea.offset.x = 0;
	rbi.renderArea.offset.y = 0;
	rbi.renderArea.extent = ctx->swapchainExtent;
	rbi.clearValueCount = 1;
	rbi.pClearValues = &cv;

	fp_vkCmdBeginRenderPass(ctx->commandBuffers[0], &rbi, VK_SUBPASS_CONTENTS_INLINE);
	ctx->started = true;
}

HL_PRIM void HL_NAME(end_render_pass)(VKContext *ctx) {
	if (!ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started && fp_vkCmdEndRenderPass) {
		fp_vkCmdEndRenderPass(ctx->commandBuffers[0]);
		ctx->started = false;
	}
}

HL_PRIM void HL_NAME(begin_rendering)(VKContext *ctx) {
	if (!ctx || !ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started) return;
	if (!fp_vkCmdBeginRendering) return;
	if (ctx->swapchainExtent.width == 0 || ctx->swapchainExtent.height == 0) return;
	if (ctx->currentImageIndex >= ctx->swapchainImageCount) return;

	printf("[VKC] begin_rendering: imgIdx=%d extent=%dx%d\n", ctx->currentImageIndex, ctx->swapchainExtent.width, ctx->swapchainExtent.height);

	cmd_image_barrier(ctx, ctx->commandBuffers[0],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		ctx->swapchainImages[ctx->currentImageIndex], 0, 1, 0, 1);

	VkRenderingAttachmentInfo colorAtt = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = ctx->swapchainImageViews[ctx->currentImageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};
	colorAtt.clearValue.color.float32[0] = ctx->clearR;
	colorAtt.clearValue.color.float32[1] = ctx->clearG;
	colorAtt.clearValue.color.float32[2] = ctx->clearB;
	colorAtt.clearValue.color.float32[3] = ctx->clearA;

	VkRenderingAttachmentInfo depthAtt = {0};
	if( ctx->defaultDepthView ) {
		cmd_image_barrier(ctx, ctx->commandBuffers[0],
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			ctx->defaultDepthImage, 0, 1, 0, 1);
		depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAtt.imageView = ctx->defaultDepthView;
		depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAtt.clearValue.depthStencil.depth = 1.0f;
	}

	VkRenderingInfo ri = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {.offset = {0, 0}, .extent = ctx->swapchainExtent},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAtt,
		.pDepthAttachment = ctx->defaultDepthView ? &depthAtt : NULL,
	};

	fp_vkCmdBeginRendering(ctx->commandBuffers[0], &ri);
	ctx->started = true;
	printf("[VKC] begin_rendering: started=%d depth=%d\n", ctx->started, ctx->defaultDepthView != VK_NULL_HANDLE);
}

HL_PRIM void HL_NAME(begin_rendering_depth)(VKContext *ctx, int depthImg, int depthView) {
	if (!ctx || !ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started) return;
	if (!fp_vkCmdBeginRendering) return;
	if (ctx->swapchainExtent.width == 0 || ctx->swapchainExtent.height == 0) return;
	if (ctx->currentImageIndex >= ctx->swapchainImageCount) return;

	printf("[VKC] begin_rendering_depth: imgIdx=%d extent=%dx%d depthImg=%d depthView=0x%llx\n",
		ctx->currentImageIndex, ctx->swapchainExtent.width, ctx->swapchainExtent.height,
		depthImg, (unsigned long long)(uintptr_t)depthView);

	cmd_image_barrier(ctx, ctx->commandBuffers[0],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		ctx->swapchainImages[ctx->currentImageIndex], 0, 1, 0, 1);

	VkRenderingAttachmentInfo colorAtt = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = ctx->swapchainImageViews[ctx->currentImageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};
	colorAtt.clearValue.color.float32[0] = ctx->clearR;
	colorAtt.clearValue.color.float32[1] = ctx->clearG;
	colorAtt.clearValue.color.float32[2] = ctx->clearB;
	colorAtt.clearValue.color.float32[3] = ctx->clearA;

	VkRenderingAttachmentInfo depthAtt = {0};
	bool hasDepth = false;
	ImgInfo *dii = get_img(depthImg);
	if (dii && dii->image && dii->view) {
		cmd_image_barrier(ctx, ctx->commandBuffers[0],
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			dii->lastLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			dii->image, 0, 1, 0, 1);
		dii->lastLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAtt.imageView = dii->view;
		depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAtt.clearValue.depthStencil.depth = 1.0f;
		depthAtt.clearValue.depthStencil.stencil = 0;
		hasDepth = true;
	}

	VkRenderingInfo ri = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {.offset = {0, 0}, .extent = ctx->swapchainExtent},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAtt,
		.pDepthAttachment = hasDepth ? &depthAtt : NULL,
	};

	fp_vkCmdBeginRendering(ctx->commandBuffers[0], &ri);
	ctx->started = true;
	printf("[VKC] begin_rendering_depth: started=%d hasDepth=%d\n", ctx->started, hasDepth);
}

// END begin_rendering_depth

HL_PRIM void HL_NAME(begin_rendering_ex)(VKContext *ctx, int imgHandle, int w, int h, float cr, float cg, float cb, float ca) {
	if (!ctx || !ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started) return;
	if (!fp_vkCmdBeginRendering) return;
	ImgInfo *ii = get_img(imgHandle);
	if (!ii || !ii->image) { printf("[VKC] begin_rendering_ex: invalid img handle %d\n", imgHandle); return; }
	if (!ii->view) { printf("[VKC] begin_rendering_ex: img %d has no view\n", imgHandle); return; }

	int rw = w > 0 ? w : ii->width;
	int rh = h > 0 ? h : ii->height;
	printf("[VKC] begin_rendering_ex: img=%d %dx%d view=0x%llx\n", imgHandle, rw, rh, (unsigned long long)(uintptr_t)ii->view);

	cmd_image_barrier(ctx, ctx->commandBuffers[0],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		ii->lastLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		ii->image, 0, 1, 0, 1);
	ii->lastLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkRenderingAttachmentInfo colorAtt = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = ii->view,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = cr < -0.5f ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};
	colorAtt.clearValue.color.float32[0] = cr >= 0 ? cr : 0;
	colorAtt.clearValue.color.float32[1] = cg >= 0 ? cg : 0;
	colorAtt.clearValue.color.float32[2] = cb >= 0 ? cb : 0;
	colorAtt.clearValue.color.float32[3] = ca >= 0 ? ca : 1;

	VkRenderingInfo ri = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {.offset = {0, 0}, .extent = {(uint32_t)rw, (uint32_t)rh}},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAtt,
	};

	fp_vkCmdBeginRendering(ctx->commandBuffers[0], &ri);
	ctx->started = true;
	printf("[VKC] begin_rendering_ex: started=%d on img=%d\n", ctx->started, imgHandle);
}

HL_PRIM void HL_NAME(begin_rendering_ex_depth)(VKContext *ctx, int imgHandle, int w, int h, float cr, float cg, float cb, float ca, int depthImg, int depthView) {
	if (!ctx || !ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started) return;
	if (!fp_vkCmdBeginRendering) return;
	ImgInfo *ii = get_img(imgHandle);
	if (!ii || !ii->image) { printf("[VKC] begin_rendering_ex_depth: invalid img handle %d\n", imgHandle); return; }
	if (!ii->view) { printf("[VKC] begin_rendering_ex_depth: img %d has no view\n", imgHandle); return; }

	int rw = w > 0 ? w : ii->width;
	int rh = h > 0 ? h : ii->height;
	printf("[VKC] begin_rendering_ex_depth: img=%d %dx%d depthImg=%d depthView=0x%llx\n",
		imgHandle, rw, rh, depthImg, (unsigned long long)(uintptr_t)depthView);

	cmd_image_barrier(ctx, ctx->commandBuffers[0],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		ii->lastLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		ii->image, 0, 1, 0, 1);
	ii->lastLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkRenderingAttachmentInfo colorAtt = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = ii->view,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = cr < -0.5f ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	};
	colorAtt.clearValue.color.float32[0] = cr >= 0 ? cr : 0;
	colorAtt.clearValue.color.float32[1] = cg >= 0 ? cg : 0;
	colorAtt.clearValue.color.float32[2] = cb >= 0 ? cb : 0;
	colorAtt.clearValue.color.float32[3] = ca >= 0 ? ca : 1;

	VkRenderingAttachmentInfo depthAtt;
	ImgInfo *dii = get_img(depthImg);
	if (dii && dii->image && dii->view) {
		cmd_image_barrier(ctx, ctx->commandBuffers[0],
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			dii->lastLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			dii->image, 0, 1, 0, 1);
		dii->lastLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAtt = (VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = dii->view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue.depthStencil = {1.0f, 0},
		};
	}

	VkRenderingInfo ri = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {.offset = {0, 0}, .extent = {(uint32_t)rw, (uint32_t)rh}},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAtt,
		.pDepthAttachment = dii ? &depthAtt : NULL,
	};

	fp_vkCmdBeginRendering(ctx->commandBuffers[0], &ri);
	ctx->started = true;
	printf("[VKC] begin_rendering_ex_depth: started on img=%d depth=%s\n", imgHandle, dii?"yes":"no");
}

HL_PRIM void HL_NAME(end_rendering)(VKContext *ctx) {
	if (!ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started && fp_vkCmdEndRendering) {
		printf("[VKC] end_rendering\n");
		fp_vkCmdEndRendering(ctx->commandBuffers[0]);
		cmd_image_barrier(ctx, ctx->commandBuffers[0],
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			ctx->swapchainImages[ctx->currentImageIndex], 0, 1, 0, 1);
		ctx->started = false;
	}
}

HL_PRIM void HL_NAME(end_rendering_ex)(VKContext *ctx, int imgHandle) {
	if (!ctx->commandBuffers || ctx->commandBufferCount == 0) return;
	if (ctx->started && fp_vkCmdEndRendering) {
		printf("[VKC] end_rendering_ex\n");
		fp_vkCmdEndRendering(ctx->commandBuffers[0]);
		ImgInfo *ii = get_img(imgHandle);
		if (ii && ii->image) {
			cmd_image_barrier(ctx, ctx->commandBuffers[0],
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				ii->image, 0, 1, 0, 1);
			ii->lastLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		ctx->started = false;
	}
}

HL_PRIM bool HL_NAME(has_dynamic_rendering)(VKContext *ctx) {
	return ctx ? ctx->hasDynamicRendering : false;
}

HL_PRIM bool HL_NAME(has_push_descriptor)(VKContext *ctx) {
	return ctx ? ctx->hasPushDescriptor : false;
}

HL_PRIM bool HL_NAME(has_extended_dynamic_states)(VKContext *ctx) {
	bool r = ctx ? ctx->hasExtendedDynamicStates : false;
	printf("[VKC] has_extended_dynamic_states=%d\n", r);
	return r;
}

HL_PRIM void HL_NAME(cmd_push_descriptor_set_texture)(VKContext *ctx, int cb, int layoutHandle, int set, int binding, int samplerId, int imgId) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdPushDescriptorSetKHR) return;
	SamplerInfo *smp = (samplerId > 0 && samplerId <= g_samplerCount) ? &g_samplers[samplerId - 1] : NULL;
	ImgInfo *imi = get_img(imgId);
	PlInfo *pli = get_pl(layoutHandle);
	if (!smp || !imi || !pli) return;
	VkDescriptorImageInfo ii = {
		.sampler = smp->sampler,
		.imageView = imi->view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet w = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = (uint32_t)binding,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo = &ii,
	};
	fp_vkCmdPushDescriptorSetKHR(ctx->commandBuffers[cb], VK_PIPELINE_BIND_POINT_GRAPHICS, pli->layout, (uint32_t)set, 1, &w);
}

HL_PRIM void HL_NAME(cmd_push_descriptor_set_buffer)(VKContext *ctx, int cb, int layoutHandle, int set, int binding, int bufId, int offset, int range) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdPushDescriptorSetKHR) return;
	BufInfo *bi = get_buf(bufId);
	PlInfo *pli = get_pl(layoutHandle);
	if (!bi || !pli) return;
	VkDescriptorBufferInfo dbi = {
		.buffer = bi->buffer,
		.offset = (VkDeviceSize)offset,
		.range = (range > 0 ? (VkDeviceSize)range : VK_WHOLE_SIZE),
	};
	VkWriteDescriptorSet w = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = (uint32_t)binding,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo = &dbi,
	};
	fp_vkCmdPushDescriptorSetKHR(ctx->commandBuffers[cb], VK_PIPELINE_BIND_POINT_GRAPHICS, pli->layout, (uint32_t)set, 1, &w);
}

#define TVK_CTX _ABSTRACT(sdl_vk_context)
#define TVK_RP _ABSTRACT(sdl_vk_renderpass)
DEFINE_PRIM(_ABSTRACT(sdl_vk_context), create_context, _ABSTRACT(sdl_window) _BOOL);
DEFINE_PRIM(_VOID, destroy_context, TVK_CTX);
DEFINE_PRIM(_VOID, wait_idle, TVK_CTX);
DEFINE_PRIM(_VOID, set_vk_vsync, _BOOL);
DEFINE_PRIM(_VOID, dirty_swapchain, TVK_CTX);
DEFINE_PRIM(_I32, get_transform, TVK_CTX);
DEFINE_PRIM(_I32, get_max_push_constants_size, TVK_CTX);
DEFINE_PRIM(_I32, get_min_ubo_alignment, TVK_CTX);
DEFINE_PRIM(_BOOL, init_swapchain, TVK_CTX _I32 _I32);
DEFINE_PRIM(_BOOL, is_swapchain_dirty, TVK_CTX);
DEFINE_PRIM(_I32, create_render_pass, TVK_CTX _BOOL _I32 _I32 _I32);
DEFINE_PRIM(_BOOL, create_framebuffers, TVK_CTX _I32);
DEFINE_PRIM(_VOID, create_descriptor_pool, TVK_CTX);
DEFINE_PRIM(_BOOL, create_command_pool, TVK_CTX);
DEFINE_PRIM(_BOOL, create_command_buffers, TVK_CTX _I32);
DEFINE_PRIM(_BOOL, begin_frame, TVK_CTX);
DEFINE_PRIM(_BOOL, end_frame, TVK_CTX);
DEFINE_PRIM(_I32, get_width, TVK_CTX);
DEFINE_PRIM(_I32, get_height, TVK_CTX);
DEFINE_PRIM(_I32, get_command_buffer, TVK_CTX _I32);
DEFINE_PRIM(_VOID, clear_color_image, TVK_CTX _F64 _F64 _F64 _F64);
DEFINE_PRIM(_BOOL, upload_texture_data, TVK_CTX _I32 _BYTES _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, begin_render_pass, TVK_CTX);
DEFINE_PRIM(_VOID, end_render_pass, TVK_CTX);
DEFINE_PRIM(_VOID, begin_rendering, TVK_CTX);
DEFINE_PRIM(_VOID, begin_rendering_depth, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, begin_rendering_ex, TVK_CTX _I32 _I32 _I32 _F32 _F32 _F32 _F32);
DEFINE_PRIM(_VOID, begin_rendering_ex_depth, TVK_CTX _I32 _I32 _I32 _F32 _F32 _F32 _F32 _I32 _I32);
DEFINE_PRIM(_VOID, end_rendering, TVK_CTX);
DEFINE_PRIM(_VOID, end_rendering_ex, TVK_CTX _I32);
DEFINE_PRIM(_BOOL, has_dynamic_rendering, TVK_CTX);
DEFINE_PRIM(_BOOL, has_push_descriptor, TVK_CTX);
DEFINE_PRIM(_BOOL, has_extended_dynamic_states, TVK_CTX);
DEFINE_PRIM(_VOID, cmd_push_descriptor_set_texture, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_push_descriptor_set_buffer, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_BOOL, begin_command_buffer, TVK_CTX _I32);
DEFINE_PRIM(_BOOL, end_command_buffer, TVK_CTX _I32);

static int add_buf(VkBuffer b, VkDeviceMemory m) {
	if (g_bufCount >= g_bufCap) { g_bufCap = g_bufCap ? g_bufCap * 2 : 16; g_bufs = (BufInfo*)realloc(g_bufs, g_bufCap * sizeof(BufInfo)); }
	int id = g_bufCount++; g_bufs[id].buffer = b; g_bufs[id].memory = m; g_bufs[id].mapped = NULL;
	return id;
}
static BufInfo* get_buf(int i) { return (i >= 0 && i < g_bufCount) ? &g_bufs[i] : NULL; }

HL_PRIM int HL_NAME(create_buffer)(VKContext *ctx, int size, int usage) {
VkBufferCreateInfo bi = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=size, .usage=usage, .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
VkBuffer b; if (fp_vkCreateBuffer(ctx->device, &bi, NULL, &b) != VK_SUCCESS) return -1;
VkMemoryRequirements mr; fp_vkGetBufferMemoryRequirements(ctx->device, b, &mr);
VkMemoryAllocateInfo ai = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=mr.size, .memoryTypeIndex=find_mem_type(ctx->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
VkDeviceMemory m; if (fp_vkAllocateMemory(ctx->device, &ai, NULL, &m) != VK_SUCCESS) { fp_vkDestroyBuffer(ctx->device, b, NULL); return -1; }
fp_vkBindBufferMemory(ctx->device, b, m, 0);
return add_buf(b, m);
}

HL_PRIM void HL_NAME(destroy_buffer)(VKContext *ctx, int bid) {
BufInfo *bi = get_buf(bid); if (!bi) return;
if (bi->mapped) fp_vkUnmapMemory(ctx->device, bi->memory);
fp_vkDestroyBuffer(ctx->device, bi->buffer, NULL);
fp_vkFreeMemory(ctx->device, bi->memory, NULL);
memset(bi, 0, sizeof(BufInfo));
}

HL_PRIM void HL_NAME(upload_buffer)(VKContext *ctx, int bid, vbyte *data, int offset, int size) {
BufInfo *bi = get_buf(bid); if (!bi) { printf("[VKC] upload_buffer FAIL: bid=%d not found\n", bid); return; }
float *f = (float*)data;
int fn = size / 4;
printf("[VKC] upload_buffer: bid=%d off=%d size=%d f0=%f f1=%f f2=%f f3=%f\n", bid, offset, size, fn>=1?f[0]:0, fn>=2?f[1]:0, fn>=3?f[2]:0, fn>=4?f[3]:0);
void *p; fp_vkMapMemory(ctx->device, bi->memory, offset, size, 0, &p);
memcpy(p, data, size);
fp_vkUnmapMemory(ctx->device, bi->memory);
}

HL_PRIM void HL_NAME(upload_buffer_floats)(VKContext *ctx, int bid, vdynamic *arr, int srcFloatOff, int dstByteOff, int floatCount) {
	BufInfo *bi = get_buf(bid); if (!bi) return;
	float *src = (float*)hl_aptr(arr, float) + srcFloatOff;
	int byteCount = floatCount * sizeof(float);
	void *p; fp_vkMapMemory(ctx->device, bi->memory, dstByteOff, byteCount, 0, &p);
	memcpy(p, src, byteCount);
	fp_vkUnmapMemory(ctx->device, bi->memory);
}

HL_PRIM void HL_NAME(upload_buffer_shorts)(VKContext *ctx, int bid, vdynamic *arr, int srcShortOff, int dstByteOff, int shortCount) {
	BufInfo *bi = get_buf(bid); if (!bi) return;
	short *src = (short*)hl_aptr(arr, short) + srcShortOff;
	int byteCount = shortCount * sizeof(short);
	void *p; fp_vkMapMemory(ctx->device, bi->memory, dstByteOff, byteCount, 0, &p);
	memcpy(p, src, byteCount);
	fp_vkUnmapMemory(ctx->device, bi->memory);
}

HL_PRIM int HL_NAME(get_buffer_handle)(VKContext *ctx, int bid) {
BufInfo *bi = get_buf(bid); return bi ? (int)(intptr_t)bi->buffer : 0;
}

// Upload large data via staging buffer + DMA (for data > 64KB)
HL_PRIM void HL_NAME(upload_buffer_staging)(VKContext *ctx, int bid, vbyte *data, int offset, int size) {
	BufInfo *bi = get_buf(bid); if (!bi || !data || size <= 0) return;

	// Create staging buffer
	VkBuffer staging;
	VkDeviceMemory stagingMem;
	VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0,
		(VkDeviceSize)size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE };
	if (fp_vkCreateBuffer(ctx->device, &bci, NULL, &staging) != VK_SUCCESS) return;

	VkMemoryRequirements mr;
	fp_vkGetBufferMemoryRequirements(ctx->device, staging, &mr);
	VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL,
		mr.size,
		find_mem_type(ctx->physicalDevice, mr.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
	if (fp_vkAllocateMemory(ctx->device, &mai, NULL, &stagingMem) != VK_SUCCESS) {
		fp_vkDestroyBuffer(ctx->device, staging, NULL); return;
	}
	fp_vkBindBufferMemory(ctx->device, staging, stagingMem, 0);

	void *mapped;
	if (fp_vkMapMemory(ctx->device, stagingMem, 0, (VkDeviceSize)size, 0, &mapped) != VK_SUCCESS) {
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL); return;
	}
	memcpy(mapped, data, (size_t)size);
	fp_vkUnmapMemory(ctx->device, stagingMem);

	// Create one-time command buffer for DMA transfer
	VkCommandPool tmpPool;
	VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
		VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, ctx->graphicsFamily };
	if (fp_vkCreateCommandPool(ctx->device, &cpi, NULL, &tmpPool) != VK_SUCCESS) {
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL); return;
	}

	VkCommandBuffer cb;
	VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL,
		tmpPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	if (fp_vkAllocateCommandBuffers(ctx->device, &ai, &cb) != VK_SUCCESS) {
		fp_vkDestroyCommandPool(ctx->device, tmpPool, NULL);
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL); return;
	}

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
	if (fp_vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS) {
		fp_vkDestroyCommandPool(ctx->device, tmpPool, NULL);
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL); return;
	}

	VkBufferCopy region = { 0, (VkDeviceSize)offset, (VkDeviceSize)size };
	fp_vkCmdCopyBuffer(cb, staging, bi->buffer, 1, &region);

	if (fp_vkEndCommandBuffer(cb) != VK_SUCCESS) {
		fp_vkDestroyCommandPool(ctx->device, tmpPool, NULL);
		fp_vkDestroyBuffer(ctx->device, staging, NULL);
		fp_vkFreeMemory(ctx->device, stagingMem, NULL); return;
	}

	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb, 0, NULL };
	fp_vkQueueSubmit(ctx->graphicsQueue, 1, &si, VK_NULL_HANDLE);
	fp_vkDeviceWaitIdle(ctx->device);

	fp_vkFreeCommandBuffers(ctx->device, tmpPool, 1, &cb);
	fp_vkDestroyCommandPool(ctx->device, tmpPool, NULL);
	fp_vkDestroyBuffer(ctx->device, staging, NULL);
	fp_vkFreeMemory(ctx->device, stagingMem, NULL);
}

HL_PRIM int HL_NAME(create_shader)(VKContext *ctx, vbyte *code, int size) {
if (size < 4 || (size & 3) != 0) return -1;
VkShaderModuleCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize=size, .pCode=(uint32_t*)code};
VkShaderModule sm; if (fp_vkCreateShaderModule(ctx->device, &ci, NULL, &sm) != VK_SUCCESS) return -1;
return add_shd(sm);
}
HL_PRIM void HL_NAME(destroy_shader)(VKContext *ctx, int s) {
ShdInfo *si = get_shd(s); if (!si) return;
fp_vkDestroyShaderModule(ctx->device, si->module, NULL);
si->module = VK_NULL_HANDLE;
}

HL_PRIM int HL_NAME(create_pipeline_layout)(VKContext *ctx, int dslTexId, int dslBufId, int dslUboId, int pcSize) {
	VkDescriptorSetLayout dslot[5] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
	uint32_t maxSet = 0;
	if (dslTexId >= 0 && dslTexId < g_dslCount) { dslot[0] = g_dsls[dslTexId]; maxSet = 1; }
	if (dslBufId >= 0 && dslBufId < g_dslCount) { dslot[1] = g_dsls[dslBufId]; maxSet = 2; }
	if (dslUboId >= 0 && dslUboId < g_dslCount) { dslot[2] = g_dsls[dslUboId]; maxSet = 3; }

	bool useUboFallback = false;
	uint32_t pcRangeSize = (uint32_t)(pcSize > 0 ? (uint32_t)pcSize : 0);

	// When push constant size exceeds hardware limit, use dynamic UBO fallback at set 3
	if (pcSize > (int)ctx->maxPushConstantsSize) {
		useUboFallback = true;
		pcRangeSize = 0;
		if (ctx->dynamicUBODslId >= 0 && ctx->dynamicUBODslId < g_dslCount) {
			dslot[3] = g_dsls[ctx->dynamicUBODslId];
			if (maxSet < 4) maxSet = 4;
		}
	}

	VkDescriptorSetLayout emptyDL[5];
	uint32_t emptyCount = 5;
	VkDescriptorSetLayoutCreateInfo eci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL };
	for (uint32_t i = 0; i < emptyCount; i++)
		fp_vkCreateDescriptorSetLayout(ctx->device, &eci, NULL, &emptyDL[i]);

	VkDescriptorSetLayout dsls[5];
	for (uint32_t i = 0; i < maxSet; i++)
		dsls[i] = (dslot[i] != VK_NULL_HANDLE) ? dslot[i] : emptyDL[i];

	VkPushConstantRange pcr = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = pcRangeSize,
	};
	VkPipelineLayoutCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = maxSet,
		.pSetLayouts = maxSet > 0 ? dsls : NULL,
		.pushConstantRangeCount = pcRangeSize > 0 ? 1u : 0u,
		.pPushConstantRanges = pcRangeSize > 0 ? &pcr : NULL,
	};
	VkPipelineLayout pl; fp_vkCreatePipelineLayout(ctx->device, &ci, NULL, &pl);

	for (uint32_t i = 0; i < emptyCount; i++)
		fp_vkDestroyDescriptorSetLayout(ctx->device, emptyDL[i], NULL);

	int plId = add_pl(pl);
	if (useUboFallback && plId >= 0) {
		PlInfo *pli = get_pl(plId);
		if (pli) pli->layout = pl; // already set by add_pl
	}
	return plId;
}
HL_PRIM void HL_NAME(destroy_pipeline_layout)(VKContext *ctx, int pl) {
PlInfo *pli = get_pl(pl); if (!pli) return;
fp_vkDestroyPipelineLayout(ctx->device, pli->layout, NULL);
pli->layout = VK_NULL_HANDLE;
}

HL_PRIM int HL_NAME(create_graphics_pipeline)(VKContext *ctx, int vs, int fs, int layout, int rp, int topo, int cull, int ff, int blend, int sblend, int dblend, int depthTest, int depthWrite, int depthCmp, int samples, int stencilFail, int stencilPass, int stencilDepthFail, int stencilCompare, int stencilRef, int colorAttachmentCount) {
	VkPipelineShaderStageCreateInfo stages[2] = {0};
	ShdInfo *vsi = get_shd(vs); if (!vsi) return -1;
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vsi->module; stages[0].pName = "main";
	if (fs != 0) {
		ShdInfo *fsi = get_shd(fs); if (!fsi) return -1;
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fsi->module; stages[1].pName = "main";
	}
	PlInfo *pli = get_pl(layout); if (!pli) return -1;
	RPInfo *rpinfo2 = get_rpinfo(rp); if (!rpinfo2) return -1;
	int stageCount = (fs != 0) ? 2 : 1;
	if (colorAttachmentCount <= 0) colorAttachmentCount = 1;
	if (colorAttachmentCount > 8) colorAttachmentCount = 8;
	VkSampleCountFlagBits sc = (VkSampleCountFlagBits)samples;

	VkVertexInputBindingDescription vibs[1];
	VkVertexInputAttributeDescription vias[2];
	int vibCount = 0, viaCount = 0;

	vibs[0].binding = 0; vibs[0].stride = 24; vibs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vias[0].location = 0; vias[0].binding = 0; vias[0].format = VK_FORMAT_R32G32B32_SFLOAT; vias[0].offset = 0;
	vias[1].location = 1; vias[1].binding = 0; vias[1].format = VK_FORMAT_R32G32B32_SFLOAT; vias[1].offset = 12;
	vibCount = 1; viaCount = 2;

	VkPipelineVertexInputStateCreateInfo vis = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, vibCount, vibs, viaCount, vias};
VkPipelineInputAssemblyStateCreateInfo ias = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, (VkPrimitiveTopology)topo, VK_FALSE};
VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, (VkCullModeFlags)cull, (VkFrontFace)ff, VK_FALSE, 0,0,0,1.0f};
VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, sc, VK_FALSE, 0, NULL, VK_FALSE, VK_FALSE};
VkPipelineColorBlendAttachmentState cba[8];
for (int ci = 0; ci < colorAttachmentCount; ci++) {
	cba[ci].blendEnable = (VkBool32)blend;
	cba[ci].srcColorBlendFactor = (VkBlendFactor)sblend; cba[ci].dstColorBlendFactor = (VkBlendFactor)dblend; cba[ci].colorBlendOp = VK_BLEND_OP_ADD;
	cba[ci].srcAlphaBlendFactor = (VkBlendFactor)sblend; cba[ci].dstAlphaBlendFactor = (VkBlendFactor)dblend; cba[ci].alphaBlendOp = VK_BLEND_OP_ADD;
	cba[ci].colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
}
VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, (uint32_t)colorAttachmentCount, cba, {0,0,0,0}};
VkStencilOpState stencilFront = {(VkStencilOp)stencilFail, (VkStencilOp)stencilPass, (VkStencilOp)stencilDepthFail, (VkCompareOp)stencilCompare, (uint32_t)stencilRef, 0xFF, 0xFF};
VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0, (VkBool32)depthTest, (VkBool32)depthWrite, (VkCompareOp)depthCmp, VK_FALSE, (VkBool32)(stencilCompare != 0 ? VK_TRUE : VK_FALSE), stencilFront, stencilFront, 0.0f, 1.0f};
VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
VkPipelineDynamicStateCreateInfo dsi = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn};
VkRenderPass pipelineRp = ctx->hasDynamicRendering ? VK_NULL_HANDLE : rpinfo2->renderPass;
VkFormat colorFormats[8];
for (int fi = 0; fi < colorAttachmentCount; fi++) colorFormats[fi] = ctx->swapchainFormat;
VkPipelineRenderingCreateInfo rendInfo = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
	.colorAttachmentCount = (uint32_t)colorAttachmentCount,
	.pColorAttachmentFormats = colorFormats,
	.depthAttachmentFormat = depthTest ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
};
VkGraphicsPipelineCreateInfo pci = {
	VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
	ctx->hasDynamicRendering ? &rendInfo : NULL,
	0, (uint32_t)stageCount, stages, &vis, &ias, NULL,
	&vps, &rs, &ms, &ds, &cb, &dsi,
	pli->layout, pipelineRp, 0, VK_NULL_HANDLE, -1
};
VkPipeline p;
VkResult res;
#ifdef HL_WIN32
	p = VK_NULL_HANDLE;
	__try {
		res = fp_vkCreateGraphicsPipelines(ctx->device, ctx->pipelineCache, 1, &pci, NULL, &p);
	} __except( GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH ) {
		res = VK_ERROR_INITIALIZATION_FAILED;
		p = VK_NULL_HANDLE;
	}
#else
	res = fp_vkCreateGraphicsPipelines(ctx->device, ctx->pipelineCache, 1, &pci, NULL, &p);
#endif
	if (res != VK_SUCCESS) return -1;
	return add_pipe(p);
}

HL_PRIM void HL_NAME(destroy_pipeline)(VKContext *ctx, int p) {
PipeInfo *pi = get_pipe(p); if (!pi) return;
fp_vkDestroyPipeline(ctx->device, pi->pipeline, NULL);
pi->pipeline = VK_NULL_HANDLE;
}

HL_PRIM int HL_NAME(create_graphics_pipeline_2d)(VKContext *ctx, int vs, int fs, int layout, int rp, int topo, int cull, int ff, int blend, int sblend, int dblend, int depthTest, int depthWrite, int depthCmp, int samples, int stencilFail, int stencilPass, int stencilDepthFail, int stencilCompare, int stencilRef, int colorAttachmentCount) {
	VkPipelineShaderStageCreateInfo stages[2] = {0};
	ShdInfo *vsi = get_shd(vs); if (!vsi) return -1;
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vsi->module; stages[0].pName = "main";
	if (fs != 0) {
		ShdInfo *fsi = get_shd(fs); if (!fsi) return -1;
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fsi->module; stages[1].pName = "main";
	}
	PlInfo *pli = get_pl(layout); if (!pli) return -1;
	RPInfo *rpinfo2 = get_rpinfo(rp); if (!rpinfo2) return -1;
	int stageCount = (fs != 0) ? 2 : 1;
	if (colorAttachmentCount <= 0) colorAttachmentCount = 1;
	if (colorAttachmentCount > 8) colorAttachmentCount = 8;
	VkSampleCountFlagBits sc = (VkSampleCountFlagBits)samples;

	VkVertexInputBindingDescription vibs[1];
	VkVertexInputAttributeDescription vias[3];
	int vibCount = 0, viaCount = 0;

	vibs[0].binding = 0; vibs[0].stride = 32; vibs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vias[0].location = 0; vias[0].binding = 0; vias[0].format = VK_FORMAT_R32G32_SFLOAT; vias[0].offset = 0;
	vias[1].location = 1; vias[1].binding = 0; vias[1].format = VK_FORMAT_R32G32_SFLOAT; vias[1].offset = 8;
	vias[2].location = 2; vias[2].binding = 0; vias[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; vias[2].offset = 16;
	vibCount = 1; viaCount = 3;

VkPipelineVertexInputStateCreateInfo vis = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, vibCount, vibs, viaCount, vias};
VkPipelineInputAssemblyStateCreateInfo ias = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, (VkPrimitiveTopology)topo, VK_FALSE};
VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, (VkCullModeFlags)cull, (VkFrontFace)ff, VK_FALSE, 0,0,0,1.0f};
VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, sc, VK_FALSE, 0, NULL, VK_FALSE, VK_FALSE};
VkPipelineColorBlendAttachmentState cba2d[8];
for (int cj = 0; cj < colorAttachmentCount; cj++) {
	cba2d[cj].blendEnable = (VkBool32)blend;
	cba2d[cj].srcColorBlendFactor = (VkBlendFactor)sblend; cba2d[cj].dstColorBlendFactor = (VkBlendFactor)dblend; cba2d[cj].colorBlendOp = VK_BLEND_OP_ADD;
	cba2d[cj].srcAlphaBlendFactor = (VkBlendFactor)sblend; cba2d[cj].dstAlphaBlendFactor = (VkBlendFactor)dblend; cba2d[cj].alphaBlendOp = VK_BLEND_OP_ADD;
	cba2d[cj].colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
}
VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, (uint32_t)colorAttachmentCount, cba2d, {0,0,0,0}};
VkStencilOpState stencilFront2d = {(VkStencilOp)stencilFail, (VkStencilOp)stencilPass, (VkStencilOp)stencilDepthFail, (VkCompareOp)stencilCompare, (uint32_t)stencilRef, 0xFF, 0xFF};
VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0, (VkBool32)depthTest, (VkBool32)depthWrite, (VkCompareOp)depthCmp, VK_FALSE, (VkBool32)(stencilCompare != 0 ? VK_TRUE : VK_FALSE), stencilFront2d, stencilFront2d, 0.0f, 1.0f};
VkDynamicState dyn2[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
VkPipelineDynamicStateCreateInfo dsi2 = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn2};
VkRenderPass pipelineRp2 = ctx->hasDynamicRendering ? VK_NULL_HANDLE : rpinfo2->renderPass;
VkFormat colorFormats2d[8];
for (int fj = 0; fj < colorAttachmentCount; fj++) colorFormats2d[fj] = ctx->swapchainFormat;
VkPipelineRenderingCreateInfo rendInfo2 = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
	.colorAttachmentCount = (uint32_t)colorAttachmentCount,
	.pColorAttachmentFormats = colorFormats2d,
	.depthAttachmentFormat = depthTest ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
};
VkGraphicsPipelineCreateInfo pci = {
	VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
	ctx->hasDynamicRendering ? &rendInfo2 : NULL,
	0, (uint32_t)stageCount, stages, &vis, &ias, NULL,
	&vps, &rs, &ms, &ds, &cb, &dsi2,
	pli->layout, pipelineRp2, 0, VK_NULL_HANDLE, -1
};
VkPipeline p;
VkResult res;
#ifdef HL_WIN32
	p = VK_NULL_HANDLE;
	__try {
		res = fp_vkCreateGraphicsPipelines(ctx->device, ctx->pipelineCache, 1, &pci, NULL, &p);
	} __except( GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH ) {
		res = VK_ERROR_INITIALIZATION_FAILED;
		p = VK_NULL_HANDLE;
	}
#else
	res = fp_vkCreateGraphicsPipelines(ctx->device, ctx->pipelineCache, 1, &pci, NULL, &p);
#endif
	if (res != VK_SUCCESS) return -1;
	return add_pipe(p);
}

HL_PRIM int HL_NAME(create_graphics_pipeline_dynamic)(VKContext *ctx, int vs, int fs, int layout, int rp, int topo, int cull, int ff, int blend, int sblend, int dblend, int depthTest, int depthWrite, int depthCmp, int samples, int stencilFail, int stencilPass, int stencilDepthFail, int stencilCompare, int stencilRef, int colorAttachmentCount, int vertStride) {
	VkPipelineShaderStageCreateInfo stages[2] = {0};
	ShdInfo *vsi = get_shd(vs); if (!vsi) return -1;
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vsi->module; stages[0].pName = "main";
	if (fs != 0) {
		ShdInfo *fsi = get_shd(fs); if (!fsi) return -1;
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fsi->module; stages[1].pName = "main";
	}
	PlInfo *pli = get_pl(layout); if (!pli) return -1;
	RPInfo *rpinfo2 = get_rpinfo(rp); if (!rpinfo2) return -1;
	int stageCount = (fs != 0) ? 2 : 1;
	if (colorAttachmentCount <= 0) colorAttachmentCount = 1;
	if (colorAttachmentCount > 8) colorAttachmentCount = 8;
	VkSampleCountFlagBits sc = (VkSampleCountFlagBits)samples;

	VkVertexInputBindingDescription vibs[1];
	VkVertexInputAttributeDescription vias[2];
	vibs[0].binding = 0; vibs[0].stride = vertStride > 0 ? vertStride : 24; vibs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	vias[0].location = 0; vias[0].binding = 0; vias[0].format = VK_FORMAT_R32G32B32_SFLOAT; vias[0].offset = 0;
	vias[1].location = 1; vias[1].binding = 0; vias[1].format = VK_FORMAT_R32G32B32_SFLOAT; vias[1].offset = 12;
	VkPipelineVertexInputStateCreateInfo vis = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 1, vibs, 2, vias};
	VkPipelineInputAssemblyStateCreateInfo ias = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, (VkPrimitiveTopology)topo, VK_FALSE};
	VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
	VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, (VkCullModeFlags)cull, (VkFrontFace)ff, VK_FALSE, 0,0,0,1.0f};
	VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, sc, VK_FALSE, 0, NULL, VK_FALSE, VK_FALSE};
	VkPipelineColorBlendAttachmentState cbad[8];
	for (int k = 0; k < colorAttachmentCount; k++) {
		cbad[k].blendEnable = (VkBool32)blend;
		cbad[k].srcColorBlendFactor = (VkBlendFactor)sblend; cbad[k].dstColorBlendFactor = (VkBlendFactor)dblend; cbad[k].colorBlendOp = VK_BLEND_OP_ADD;
		cbad[k].srcAlphaBlendFactor = (VkBlendFactor)sblend; cbad[k].dstAlphaBlendFactor = (VkBlendFactor)dblend; cbad[k].alphaBlendOp = VK_BLEND_OP_ADD;
		cbad[k].colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
	}
	VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, (uint32_t)colorAttachmentCount, cbad, {0,0,0,0}};
	VkStencilOpState st = {(VkStencilOp)stencilFail, (VkStencilOp)stencilPass, (VkStencilOp)stencilDepthFail, (VkCompareOp)stencilCompare, (uint32_t)stencilRef, 0xFF, 0xFF};
	VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0, (VkBool32)depthTest, (VkBool32)depthWrite, (VkCompareOp)depthCmp, VK_FALSE, (VkBool32)(stencilCompare != 0 ? VK_TRUE : VK_FALSE), st, st, 0.0f, 1.0f};
	VkDynamicState dynd[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
	VkPipelineDynamicStateCreateInfo dsi = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 8, dynd};
	VkRenderPass pipelineRp = ctx->hasDynamicRendering ? VK_NULL_HANDLE : rpinfo2->renderPass;
	VkFormat colorFormatsD[8];
	for (int fj = 0; fj < colorAttachmentCount; fj++) colorFormatsD[fj] = ctx->swapchainFormat;
	VkPipelineRenderingCreateInfo rendInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = (uint32_t)colorAttachmentCount, .pColorAttachmentFormats = colorFormatsD, .depthAttachmentFormat = depthTest ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED };
	VkGraphicsPipelineCreateInfo pci = {
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, ctx->hasDynamicRendering ? &rendInfo : NULL, 0, (uint32_t)stageCount, stages, &vis, &ias, NULL,
		&vps, &rs, &ms, &ds, &cb, &dsi, pli->layout, pipelineRp, 0, VK_NULL_HANDLE, -1
	};
	VkPipeline p;
	VkResult pres = fp_vkCreateGraphicsPipelines(ctx->device, ctx->pipelineCache, 1, &pci, NULL, &p);
	printf("[VKC] create_pipeline_vi: result=%d pipe=0x%llx\n", pres, (unsigned long long)(uintptr_t)p);
	if (pres != VK_SUCCESS) return -1;
	return add_pipe(p);
}

// create_graphics_pipeline_dynamic with explicit vertex input attributes
HL_PRIM int HL_NAME(create_graphics_pipeline_dynamic_vi)(VKContext *ctx, int vs, int fs, int layout, int rp, int topo, int cull, int ff, int blend, int sblend, int dblend, int depthTest, int depthWrite, int depthCmp, int samples, int stencilFail, int stencilPass, int stencilDepthFail, int stencilCompare, int stencilRef, int colorAttachmentCount, int vertStride, int attrCount, vbyte *attrData) {
	printf("[VKC] create_graphics_pipeline_dynamic_vi ENTERED\n");
	fflush(stdout);
 	if (!ctx || !ctx->device) { printf("[VKC] VI FAIL: no ctx/device\n"); return -1; }
 	if (colorAttachmentCount <= 0) colorAttachmentCount = 1;
 	if (colorAttachmentCount > 8) colorAttachmentCount = 8;
 	VkSampleCountFlagBits sc = (VkSampleCountFlagBits)samples;
	printf("[VKC] create_pipeline_vi: stride=%d attrCount=%d\n", vertStride, attrCount);

	VkPipelineShaderStageCreateInfo stages[2] = {0};
	ShdInfo *vsi = get_shd(vs); if (!vsi) return -1;
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vsi->module; stages[0].pName = "main";
	if (fs != 0) {
		ShdInfo *fsi = get_shd(fs); if (!fsi) return -1;
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fsi->module; stages[1].pName = "main";
	}
	int stageCount = (fs != 0) ? 2 : 1;
 	PlInfo *pli = get_pl(layout); if (!pli) return -1;
 	RPInfo *rpinfo2 = get_rpinfo(rp);

	VkVertexInputBindingDescription vibs[16];
	VkVertexInputAttributeDescription vias[16];
	int vibCount = 1, viaCount = attrCount;
	vibs[0].binding = 0; vibs[0].stride = (uint32_t)vertStride; vibs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	int *attr = (int*)attrData;
	for (int i = 0; i < attrCount && i < 16; i++) {
		vias[i].location = (uint32_t)attr[i * 3];
		vias[i].binding = 0;
		vias[i].format = (VkFormat)attr[i * 3 + 1];
		vias[i].offset = (uint32_t)attr[i * 3 + 2];
	}
	VkPipelineVertexInputStateCreateInfo vis = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, vibCount, vibs, viaCount, vias};
	VkPipelineInputAssemblyStateCreateInfo ias = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, (VkPrimitiveTopology)topo, VK_FALSE};
	VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
	VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, (VkCullModeFlags)cull, (VkFrontFace)ff, VK_FALSE, 0,0,0,1.0f};
	VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, sc, VK_FALSE, 0, NULL, VK_FALSE, VK_FALSE};
	VkPipelineColorBlendAttachmentState cbad[8];
	for (int k = 0; k < colorAttachmentCount; k++) {
		cbad[k].blendEnable = (VkBool32)blend;
		cbad[k].srcColorBlendFactor = (VkBlendFactor)sblend; cbad[k].dstColorBlendFactor = (VkBlendFactor)dblend; cbad[k].colorBlendOp = VK_BLEND_OP_ADD;
		cbad[k].srcAlphaBlendFactor = (VkBlendFactor)sblend; cbad[k].dstAlphaBlendFactor = (VkBlendFactor)dblend; cbad[k].alphaBlendOp = VK_BLEND_OP_ADD;
		cbad[k].colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
	}
	VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, (uint32_t)colorAttachmentCount, cbad, {0,0,0,0}};
	VkStencilOpState st = {(VkStencilOp)stencilFail, (VkStencilOp)stencilPass, (VkStencilOp)stencilDepthFail, (VkCompareOp)stencilCompare, (uint32_t)stencilRef, 0xFF, 0xFF};
	VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, NULL, 0, (VkBool32)depthTest, (VkBool32)depthWrite, (VkCompareOp)depthCmp, VK_FALSE, (VkBool32)(stencilCompare != 0 ? VK_TRUE : VK_FALSE), st, st, 0.0f, 1.0f};
	VkDynamicState dynd[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
	VkPipelineDynamicStateCreateInfo dsi = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 8, dynd};
	VkRenderPass pipelineRp = ctx->hasDynamicRendering ? VK_NULL_HANDLE : rpinfo2->renderPass;
	VkFormat colorFormatsD[8];
	for (int fj = 0; fj < colorAttachmentCount; fj++) colorFormatsD[fj] = ctx->swapchainFormat;
	VkPipelineRenderingCreateInfo rendInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = (uint32_t)colorAttachmentCount, .pColorAttachmentFormats = colorFormatsD, .depthAttachmentFormat = depthTest ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED };
	VkGraphicsPipelineCreateInfo pci = {
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, ctx->hasDynamicRendering ? &rendInfo : NULL, 0, (uint32_t)stageCount, stages, &vis, &ias, NULL,
		&vps, &rs, &ms, &ds, &cb, &dsi, pli->layout, pipelineRp, 0, VK_NULL_HANDLE, -1
	};
	VkPipeline p;
	if (fp_vkCreateGraphicsPipelines(ctx->device, ctx->pipelineCache, 1, &pci, NULL, &p) != VK_SUCCESS) return -1;
	return add_pipe(p);
}

HL_PRIM void HL_NAME(cmd_bind_pipeline)(VKContext *ctx, int cb, int pipe) {
if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) { printf("[VKC] bind_pipeline FAIL: no cmdbuf\n"); return; }
PipeInfo *pi = get_pipe(pipe); if (!pi) { printf("[VKC] bind_pipeline FAIL: pipe %d not found\n", pipe); return; }
printf("[VKC] bind_pipeline: pipe=%d handle=0x%llx\n", pipe, (unsigned long long)(uintptr_t)pi->pipeline);
fp_vkCmdBindPipeline(ctx->commandBuffers[cb], VK_PIPELINE_BIND_POINT_GRAPHICS, pi->pipeline);
}
HL_PRIM void HL_NAME(cmd_bind_vertex_buffer)(VKContext *ctx, int cb, int binding, int bid, int offset) {
if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
BufInfo *bi = get_buf(bid); if (!bi) { printf("[VKC] bind_vb FAIL: buf %d not found\n", bid); return; }
printf("[VKC] bind_vb: binding=%d bid=%d handle=0x%llx off=%d\n", binding, bid, (unsigned long long)(uintptr_t)bi->buffer, offset);
VkDeviceSize o = offset; fp_vkCmdBindVertexBuffers(ctx->commandBuffers[cb], binding, 1, &bi->buffer, &o);
}
HL_PRIM void HL_NAME(cmd_bind_index_buffer)(VKContext *ctx, int cb, int bid, int offset, int idxType) {
if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
BufInfo *bi = get_buf(bid); if (!bi) { printf("[VKC] bind_ib FAIL: buf %d not found\n", bid); return; }
printf("[VKC] bind_ib: bid=%d handle=0x%llx off=%d type=%d\n", bid, (unsigned long long)(uintptr_t)bi->buffer, offset, idxType);
fp_vkCmdBindIndexBuffer(ctx->commandBuffers[cb], bi->buffer, offset, (VkIndexType)idxType);
}
HL_PRIM void HL_NAME(cmd_set_viewport)(VKContext *ctx, int cb, float x, float y, float w, float h) {
if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
printf("[VKC] viewport: x=%f y=%f w=%f h=%f\n", x, y, w, h);
VkViewport vp = {x, y, w, h, 0.0f, 1.0f}; fp_vkCmdSetViewport(ctx->commandBuffers[cb], 0, 1, &vp);
}
HL_PRIM void HL_NAME(cmd_set_scissor)(VKContext *ctx, int cb, int x, int y, int w, int h) {
if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
printf("[VKC] scissor: x=%d y=%d w=%d h=%d\n", x, y, w, h);
VkRect2D sc = {{x, y}, {w, h}}; fp_vkCmdSetScissor(ctx->commandBuffers[cb], 0, 1, &sc);
}
HL_PRIM void HL_NAME(cmd_set_line_width)(VKContext *ctx, int cb, float lineWidth) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetLineWidth) fp_vkCmdSetLineWidth(ctx->commandBuffers[cb], lineWidth);
}
HL_PRIM void HL_NAME(cmd_set_stencil_reference)(VKContext *ctx, int cb, int faceMask, int reference) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetStencilReference) fp_vkCmdSetStencilReference(ctx->commandBuffers[cb], (VkStencilFaceFlags)faceMask, (uint32_t)reference);
}
HL_PRIM void HL_NAME(cmd_set_cull_mode)(VKContext *ctx, int cb, int cullMode) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetCullMode) fp_vkCmdSetCullMode(ctx->commandBuffers[cb], (VkCullModeFlags)cullMode);
}
HL_PRIM void HL_NAME(cmd_set_front_face)(VKContext *ctx, int cb, int frontFace) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetFrontFace) fp_vkCmdSetFrontFace(ctx->commandBuffers[cb], (VkFrontFace)frontFace);
}
HL_PRIM void HL_NAME(cmd_set_primitive_topology)(VKContext *ctx, int cb, int topo) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetPrimitiveTopology) fp_vkCmdSetPrimitiveTopology(ctx->commandBuffers[cb], (VkPrimitiveTopology)topo);
}
HL_PRIM void HL_NAME(cmd_set_depth_test_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetDepthTestEnable) fp_vkCmdSetDepthTestEnable(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_depth_write_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetDepthWriteEnable) fp_vkCmdSetDepthWriteEnable(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_depth_compare_op)(VKContext *ctx, int cb, int compareOp) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetDepthCompareOp) fp_vkCmdSetDepthCompareOp(ctx->commandBuffers[cb], (VkCompareOp)compareOp);
}
HL_PRIM void HL_NAME(cmd_set_stencil_test_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetStencilTestEnable) fp_vkCmdSetStencilTestEnable(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_depth_bias_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetDepthBiasEnable) fp_vkCmdSetDepthBiasEnable(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_rasterizer_discard_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetRasterizerDiscardEnable) fp_vkCmdSetRasterizerDiscardEnable(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM bool HL_NAME(check_descriptor_set_layout_support)(VKContext *ctx, int bindingCount, int bindingType) {
	if (!ctx || !ctx->device || !fp_vkGetDescriptorSetLayoutSupport) return true;
	VkDescriptorSetLayoutBinding *bindings = NULL;
	VkDescriptorType dtype = (bindingType == 0) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	if (bindingCount > 0) {
		bindings = (VkDescriptorSetLayoutBinding*)calloc(bindingCount, sizeof(VkDescriptorSetLayoutBinding));
		for (int i = 0; i < bindingCount; i++) { bindings[i].binding = (uint32_t)i; bindings[i].descriptorType = dtype; bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_ALL; }
	}
	VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, (uint32_t)bindingCount, bindings };
	VkDescriptorSetLayoutSupport support = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT };
	fp_vkGetDescriptorSetLayoutSupport(ctx->device, &dslci, &support);
	free(bindings);
	return support.supported == VK_TRUE;
}
HL_PRIM void HL_NAME(host_reset_query_pool)(VKContext *ctx, int qpId, int firstQuery, int queryCount) {
	if (!ctx || !ctx->device || !fp_vkResetQueryPool) return;
	int id = qpId - 1;
	if (id < 0 || id >= g_queryCount || !g_queries[id]) return;
	fp_vkResetQueryPool(ctx->device, g_queries[id], (uint32_t)firstQuery, (uint32_t)queryCount);
}
HL_PRIM void HL_NAME(cmd_push_constants2)(VKContext *ctx, int cb, int layoutHandle, int stageFlags, int offset, int size, vbyte *data) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!data || size <= 0 || offset < 0) return;
	if ((uint32_t)offset >= ctx->maxPushConstantsSize) return;
	if ((uint32_t)(offset + size) > ctx->maxPushConstantsSize)
		size = (int)(ctx->maxPushConstantsSize - (uint32_t)offset);
	if (size <= 0) return;
	if (fp_vkCmdPushConstants2KHR) {
		VkPushConstantsInfoKHR pci = { VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR, NULL, (VkPipelineLayout)(intptr_t)layoutHandle, (VkShaderStageFlags)stageFlags, (uint32_t)offset, (uint32_t)size, data };
		fp_vkCmdPushConstants2KHR(ctx->commandBuffers[cb], &pci);
	} else {
		PlInfo *pli = get_pl(layoutHandle);
		if (!pli) return;
		fp_vkCmdPushConstants(ctx->commandBuffers[cb], pli->layout, (VkShaderStageFlags)stageFlags, (uint32_t)offset, (uint32_t)size, data);
	}
}
HL_PRIM void HL_NAME(cmd_copy_image)(VKContext *ctx, int cb, int srcImgId, int dstImgId, int srcLayout, int dstLayout) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	ImgInfo *src = get_img(srcImgId);
	ImgInfo *dst = get_img(dstImgId);
	if (!src || !dst) return;
	VkImageCopy region = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.extent = { (uint32_t)src->width, (uint32_t)src->height, 1 },
	};
	fp_vkCmdCopyImage(ctx->commandBuffers[cb], src->image, (VkImageLayout)srcLayout, dst->image, (VkImageLayout)dstLayout, 1, &region);
}
HL_PRIM void HL_NAME(cmd_copy_image_to_buffer)(VKContext *ctx, int cb, int imgId, int layout, int bufId) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	ImgInfo *ii = get_img(imgId);
	BufInfo *bi = get_buf(bufId);
	if (!ii || !bi) return;
	VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { (uint32_t)ii->width, (uint32_t)ii->height, 1 } };
	fp_vkCmdCopyImageToBuffer(ctx->commandBuffers[cb], ii->image, (VkImageLayout)layout, bi->buffer, 1, &region);
}
HL_PRIM void HL_NAME(cmd_clear_depth_stencil)(VKContext *ctx, int cb, int imgId, int layout, float depth, int stencil, int aspects) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	ImgInfo *ii = get_img(imgId);
	if (!ii) return;
	VkClearDepthStencilValue ds = { depth, (uint32_t)stencil };
	VkImageSubresourceRange range = { (VkImageAspectFlags)aspects, 0, 1, 0, 1 };
	fp_vkCmdClearDepthStencilImage(ctx->commandBuffers[cb], ii->image, (VkImageLayout)layout, &ds, 1, &range);
}
HL_PRIM void HL_NAME(cmd_set_blend_constants)(VKContext *ctx, int cb, float r, float g, float b, float a) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	float bc[4] = { r, g, b, a };
	fp_vkCmdSetBlendConstants(ctx->commandBuffers[cb], bc);
}
HL_PRIM int HL_NAME(create_event)(VKContext *ctx) {
	if (!ctx || !ctx->device || !fp_vkCreateEvent) return -1;
	VkEventCreateInfo ei = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
	VkEvent ev;
	if (fp_vkCreateEvent(ctx->device, &ei, NULL, &ev) != VK_SUCCESS) return -1;
	if (g_evtCount >= g_evtCap) { g_evtCap = g_evtCap ? g_evtCap * 2 : 8; g_evts = (VkEvent*)realloc(g_evts, g_evtCap * sizeof(VkEvent)); }
	int id = g_evtCount++; g_evts[id] = ev;
	return id + 1;
}
HL_PRIM void HL_NAME(destroy_event)(VKContext *ctx, int evtId) {
	if (!ctx || !fp_vkDestroyEvent) return;
	int id = evtId - 1;
	if (id >= 0 && id < g_evtCount && g_evts[id]) {
		fp_vkDestroyEvent(ctx->device, g_evts[id], NULL);
		g_evts[id] = VK_NULL_HANDLE;
	}
}
HL_PRIM void HL_NAME(cmd_set_event)(VKContext *ctx, int cb, int evtId, int stage) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdSetEvent || !fp_vkCreateEvent) return;
	int id = evtId - 1;
	if (id >= 0 && id < g_evtCount && g_evts[id])
		fp_vkCmdSetEvent(ctx->commandBuffers[cb], g_evts[id], (VkPipelineStageFlags)stage);
}
HL_PRIM void HL_NAME(cmd_wait_events)(VKContext *ctx, int cb, int evtId, int srcStage, int dstStage) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdWaitEvents) return;
	int id = evtId - 1;
	if (id >= 0 && id < g_evtCount && g_evts[id])
		fp_vkCmdWaitEvents(ctx->commandBuffers[cb], 1, &g_evts[id], (VkPipelineStageFlags)srcStage, (VkPipelineStageFlags)dstStage, 0, NULL, 0, NULL, 0, NULL);
}
HL_PRIM void HL_NAME(flush_memory)(VKContext *ctx, int bufId) {
	if (!ctx || !ctx->device || !fp_vkFlushMappedMemoryRanges) return;
	BufInfo *bi = get_buf(bufId);
	if (!bi) return;
	VkMappedMemoryRange mr = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, bi->memory, 0, VK_WHOLE_SIZE };
	fp_vkFlushMappedMemoryRanges(ctx->device, 1, &mr);
}
HL_PRIM void HL_NAME(invalidate_memory)(VKContext *ctx, int bufId) {
	if (!ctx || !ctx->device || !fp_vkInvalidateMappedMemoryRanges) return;
	BufInfo *bi = get_buf(bufId);
	if (!bi) return;
	VkMappedMemoryRange mr = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, bi->memory, 0, VK_WHOLE_SIZE };
	fp_vkInvalidateMappedMemoryRanges(ctx->device, 1, &mr);
}
HL_PRIM void HL_NAME(bind_image_memory2)(VKContext *ctx, int imgId, int memId) {
	if (!ctx || !ctx->device || !fp_vkBindImageMemory2) return;
	ImgInfo *ii = get_img(imgId);
	if (!ii) return;
	VkBindImageMemoryInfo bi2 = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, NULL, ii->image, (VkDeviceMemory)(intptr_t)memId, 0 };
	fp_vkBindImageMemory2(ctx->device, 1, &bi2);
}
HL_PRIM void HL_NAME(cmd_set_polygon_mode)(VKContext *ctx, int cb, int mode) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetPolygonModeEXT) fp_vkCmdSetPolygonModeEXT(ctx->commandBuffers[cb], (VkPolygonMode)mode);
}
HL_PRIM void HL_NAME(cmd_set_primitive_restart_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetPrimitiveRestartEnableEXT) fp_vkCmdSetPrimitiveRestartEnableEXT(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_rasterization_samples)(VKContext *ctx, int cb, int samples) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetRasterizationSamplesEXT) fp_vkCmdSetRasterizationSamplesEXT(ctx->commandBuffers[cb], (VkSampleCountFlagBits)samples);
}
HL_PRIM void HL_NAME(cmd_draw_indirect_byte_count)(VKContext *ctx, int cb, int bufId, int offset, int countBufId, int countOffset, int maxDrawCount, int stride) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	BufInfo *bi = get_buf(bufId);
	BufInfo *cbi = get_buf(countBufId);
	if (!bi || !bi->buffer || !cbi || !cbi->buffer) return;
	if (fp_vkCmdDrawIndirectByteCountEXT)
		fp_vkCmdDrawIndirectByteCountEXT(ctx->commandBuffers[cb], (uint32_t)maxDrawCount, 0, cbi->buffer, (VkDeviceSize)countOffset, (uint32_t)offset, (uint32_t)stride);
}
HL_PRIM void HL_NAME(cmd_set_logic_op_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetLogicOpEnableEXT) fp_vkCmdSetLogicOpEnableEXT(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_logic_op)(VKContext *ctx, int cb, int op) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetLogicOpEXT) fp_vkCmdSetLogicOpEXT(ctx->commandBuffers[cb], (VkLogicOp)op);
}
HL_PRIM void HL_NAME(cmd_set_color_blend_enable)(VKContext *ctx, int cb, int firstAtt, int count, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetColorBlendEnableEXT) {
		VkBool32 v = enable ? VK_TRUE : VK_FALSE;
		VkBool32 vals[8]; for (int i = 0; i < count && i < 8; i++) vals[i] = v;
		fp_vkCmdSetColorBlendEnableEXT(ctx->commandBuffers[cb], (uint32_t)firstAtt, (uint32_t)count, vals);
	}
}
HL_PRIM void HL_NAME(cmd_set_color_blend_equation)(VKContext *ctx, int cb, int firstAtt, int count, int mode, int src, int dst, int alphaMode, int alphaSrc, int alphaDst) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetColorBlendEquationEXT) {
		VkColorBlendEquationEXT eqs[8];
		for (int i = 0; i < count && i < 8; i++) {
			eqs[i].srcColorBlendFactor = (VkBlendFactor)src; eqs[i].dstColorBlendFactor = (VkBlendFactor)dst;
			eqs[i].colorBlendOp = (VkBlendOp)mode; eqs[i].srcAlphaBlendFactor = (VkBlendFactor)alphaSrc;
			eqs[i].dstAlphaBlendFactor = (VkBlendFactor)alphaDst; eqs[i].alphaBlendOp = (VkBlendOp)alphaMode;
		}
		fp_vkCmdSetColorBlendEquationEXT(ctx->commandBuffers[cb], (uint32_t)firstAtt, (uint32_t)count, eqs);
	}
}
HL_PRIM void HL_NAME(cmd_set_color_write_mask)(VKContext *ctx, int cb, int firstAtt, int count, int mask) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetColorWriteMaskEXT) {
		VkColorComponentFlags masks[8];
		for (int i = 0; i < count && i < 8; i++) masks[i] = (VkColorComponentFlags)mask;
		fp_vkCmdSetColorWriteMaskEXT(ctx->commandBuffers[cb], (uint32_t)firstAtt, (uint32_t)count, masks);
	}
}
HL_PRIM void HL_NAME(cmd_set_depth_clamp_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetDepthClampEnableEXT) fp_vkCmdSetDepthClampEnableEXT(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_provoking_vertex_mode)(VKContext *ctx, int cb, int mode) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetProvokingVertexModeEXT) fp_vkCmdSetProvokingVertexModeEXT(ctx->commandBuffers[cb], (VkProvokingVertexModeEXT)mode);
}
HL_PRIM void HL_NAME(cmd_set_line_rasterization_mode)(VKContext *ctx, int cb, int mode) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetLineRasterizationModeEXT) fp_vkCmdSetLineRasterizationModeEXT(ctx->commandBuffers[cb], (VkLineRasterizationModeEXT)mode);
}
HL_PRIM void HL_NAME(cmd_set_tessellation_domain_origin)(VKContext *ctx, int cb, int origin) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetTessellationDomainOriginEXT) fp_vkCmdSetTessellationDomainOriginEXT(ctx->commandBuffers[cb], (VkTessellationDomainOrigin)origin);
}
HL_PRIM void HL_NAME(cmd_copy_buffer2)(VKContext *ctx, int cb, int srcBufId, int dstBufId, int size) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdCopyBuffer2) return;
	BufInfo *src = get_buf(srcBufId);
	BufInfo *dst = get_buf(dstBufId);
	if (!src || !dst) return;
	VkBufferCopy2 region = { .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2, .size = (VkDeviceSize)size };
	VkCopyBufferInfo2 info = { .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2, .srcBuffer = src->buffer, .dstBuffer = dst->buffer, .regionCount = 1, .pRegions = &region };
	fp_vkCmdCopyBuffer2(ctx->commandBuffers[cb], &info);
}
HL_PRIM void HL_NAME(cmd_copy_image2)(VKContext *ctx, int cb, int srcImgId, int dstImgId, int srcLayout, int dstLayout) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdCopyImage2) return;
	ImgInfo *src = get_img(srcImgId);
	ImgInfo *dst = get_img(dstImgId);
	if (!src || !dst) return;
	VkImageCopy2 region = { .sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2,
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.extent = { (uint32_t)src->width, (uint32_t)src->height, 1 } };
	VkCopyImageInfo2 info = { .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2,
		.srcImage = src->image, .srcImageLayout = (VkImageLayout)srcLayout,
		.dstImage = dst->image, .dstImageLayout = (VkImageLayout)dstLayout,
		.regionCount = 1, .pRegions = &region };
	fp_vkCmdCopyImage2(ctx->commandBuffers[cb], &info);
}
HL_PRIM void HL_NAME(cmd_blit_image2)(VKContext *ctx, int cb, int srcImgId, int dstImgId, int srcMip, int dstMip) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdBlitImage2) return;
	ImgInfo *src = get_img(srcImgId);
	ImgInfo *dst = get_img(dstImgId);
	if (!src || !dst) return;
	VkImageBlit2 region = { .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)srcMip, 0, 1 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)dstMip, 0, 1 } };
	VkBlitImageInfo2 info = { .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
		.srcImage = src->image, .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.dstImage = dst->image, .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.regionCount = 1, .pRegions = &region, .filter = VK_FILTER_LINEAR };
	fp_vkCmdBlitImage2(ctx->commandBuffers[cb], &info);
}
HL_PRIM void HL_NAME(set_debug_name)(VKContext *ctx, int objHandle, int objType, vbyte *name) {
	if (!ctx || !ctx->device || !fp_vkSetDebugUtilsObjectNameEXT || !name) return;
	VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		NULL, (VkObjectType)objType, (uint64_t)(intptr_t)objHandle, (const char*)name };
	fp_vkSetDebugUtilsObjectNameEXT(ctx->device, &nameInfo);
}
HL_PRIM void HL_NAME(cmd_begin_debug_label)(VKContext *ctx, int cb, float r, float g, float b, float a, vbyte *name) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdBeginDebugUtilsLabelEXT || !name) return;
	VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, NULL, (const char*)name, {r,g,b,a} };
	fp_vkCmdBeginDebugUtilsLabelEXT(ctx->commandBuffers[cb], &label);
}
HL_PRIM void HL_NAME(cmd_end_debug_label)(VKContext *ctx, int cb) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdEndDebugUtilsLabelEXT) fp_vkCmdEndDebugUtilsLabelEXT(ctx->commandBuffers[cb]);
}
HL_PRIM void HL_NAME(cmd_reset_event)(VKContext *ctx, int cb, int evtId, int stage) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdResetEvent) return;
	int id = evtId - 1;
	if (id >= 0 && id < g_evtCount && g_evts[id])
		fp_vkCmdResetEvent(ctx->commandBuffers[cb], g_evts[id], (VkPipelineStageFlags)stage);
}
HL_PRIM bool HL_NAME(get_event_status)(VKContext *ctx, int evtId) {
	if (!ctx || !ctx->device || !fp_vkGetEventStatus) return false;
	int id = evtId - 1;
	if (id >= 0 && id < g_evtCount && g_evts[id])
		return fp_vkGetEventStatus(ctx->device, g_evts[id]) == VK_EVENT_SET;
	return false;
}
HL_PRIM void HL_NAME(cmd_begin_conditional_rendering)(VKContext *ctx, int cb, int bufId, int offset, bool inverted) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdBeginConditionalRenderingEXT) return;
	BufInfo *bi = get_buf(bufId);
	if (!bi || !bi->buffer) return;
	VkConditionalRenderingBeginInfoEXT crbi = { VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT,
		NULL, bi->buffer, (VkDeviceSize)offset, inverted ? VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT : 0 };
	fp_vkCmdBeginConditionalRenderingEXT(ctx->commandBuffers[cb], &crbi);
}
HL_PRIM void HL_NAME(cmd_end_conditional_rendering)(VKContext *ctx, int cb) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdEndConditionalRenderingEXT) fp_vkCmdEndConditionalRenderingEXT(ctx->commandBuffers[cb]);
}
HL_PRIM void HL_NAME(cmd_set_alpha_to_one_enable)(VKContext *ctx, int cb, bool enable) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (fp_vkCmdSetAlphaToOneEnableEXT) fp_vkCmdSetAlphaToOneEnableEXT(ctx->commandBuffers[cb], enable ? VK_TRUE : VK_FALSE);
}
HL_PRIM void HL_NAME(cmd_set_fragment_shading_rate)(VKContext *ctx, int cb, int sizeW, int sizeH) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdSetFragmentShadingRateKHR) return;
	VkExtent2D fragmentSize = { (uint32_t)sizeW, (uint32_t)sizeH };
	VkFragmentShadingRateCombinerOpKHR combinerOps[2] = { VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR };
	fp_vkCmdSetFragmentShadingRateKHR(ctx->commandBuffers[cb], &fragmentSize, combinerOps);
}
HL_PRIM void HL_NAME(cmd_set_sample_locations)(VKContext *ctx, int cb, int samples) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!fp_vkCmdSetSampleLocationsEXT) return;
	VkSampleLocationsInfoEXT sl;
	memset(&sl, 0, sizeof(sl));
	sl.sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT;
	sl.sampleLocationsPerPixel = (VkSampleCountFlagBits)samples;
	fp_vkCmdSetSampleLocationsEXT(ctx->commandBuffers[cb], &sl);
}
HL_PRIM int HL_NAME(create_descriptor_update_template)(VKContext *ctx, int dslId, int pipelineLayout, int set, int bindingCount) {
	if (!ctx || !ctx->device || !fp_vkCreateDescriptorUpdateTemplate) return -1;
	if (dslId < 0 || dslId >= g_dslCount || !g_dsls[dslId]) return -1;
	VkDescriptorUpdateTemplateEntry *entries = NULL;
	if (bindingCount > 0) {
		entries = (VkDescriptorUpdateTemplateEntry*)calloc(bindingCount, sizeof(VkDescriptorUpdateTemplateEntry));
		for (int i = 0; i < bindingCount; i++) {
			entries[i].dstBinding = (uint32_t)i;
			entries[i].dstArrayElement = 0;
			entries[i].descriptorCount = 1;
			entries[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		}
	}
	VkDescriptorUpdateTemplateCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
		NULL, 0, (uint32_t)bindingCount, entries, VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
		g_dsls[dslId], 0, (VkPipelineLayout)(intptr_t)pipelineLayout, (uint32_t)set };
	VkDescriptorUpdateTemplate dut;
	VkResult res = fp_vkCreateDescriptorUpdateTemplate(ctx->device, &ci, NULL, &dut);
	free(entries);
	if (res != VK_SUCCESS) return -1;
	if (g_tplCount >= g_tplCap) { g_tplCap = g_tplCap ? g_tplCap * 2 : 8; g_tpls = (VkDescriptorUpdateTemplate*)realloc(g_tpls, g_tplCap * sizeof(VkDescriptorUpdateTemplate)); }
	int id = g_tplCount++; g_tpls[id] = dut;
	return id + 1;
}
HL_PRIM void HL_NAME(destroy_descriptor_update_template)(VKContext *ctx, int id) {
	if (!ctx || !fp_vkDestroyDescriptorUpdateTemplate) return;
	int idx = id - 1;
	if (idx >= 0 && idx < g_tplCount && g_tpls[idx]) {
		fp_vkDestroyDescriptorUpdateTemplate(ctx->device, g_tpls[idx], NULL);
		g_tpls[idx] = VK_NULL_HANDLE;
	}
}
HL_PRIM void HL_NAME(cmd_draw)(VKContext *ctx, int cb, int vc, int ic, int fv, int fi) {
if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
fp_vkCmdDraw(ctx->commandBuffers[cb], vc, ic, fv, fi);
}
HL_PRIM void HL_NAME(cmd_draw_indexed)(VKContext *ctx, int cb, int idxCount, int instCount, int firstIdx, int vertOff, int firstInst) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	printf("[VKC] draw_indexed: cb=%d idx=%d inst=%d firstIdx=%d vo=%d fi=%d\n", cb, idxCount, instCount, firstIdx, vertOff, firstInst);
	fp_vkCmdDrawIndexed(ctx->commandBuffers[cb], idxCount, instCount, firstIdx, vertOff, firstInst);
}

HL_PRIM void HL_NAME(cmd_draw_indexed_indirect)(VKContext *ctx, int cb, int bufId, int offset, int drawCount, int stride) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	BufInfo *bi = get_buf(bufId);
	if (!bi || !bi->buffer) return;
	fp_vkCmdDrawIndexedIndirect(ctx->commandBuffers[cb], bi->buffer, (VkDeviceSize)offset, (uint32_t)drawCount, (uint32_t)stride);
}

HL_PRIM void HL_NAME(cmd_draw_indirect_count)(VKContext *ctx, int cb, int bufId, int offset, int countBufId, int countOffset, int maxDrawCount, int stride) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	BufInfo *bi = get_buf(bufId);
	BufInfo *cbi = get_buf(countBufId);
	if (!bi || !bi->buffer || !cbi || !cbi->buffer) return;
	if (fp_vkCmdDrawIndirectCount)
		fp_vkCmdDrawIndirectCount(ctx->commandBuffers[cb], bi->buffer, (VkDeviceSize)offset, cbi->buffer, (VkDeviceSize)countOffset, (uint32_t)maxDrawCount, (uint32_t)stride);
}

HL_PRIM int HL_NAME(create_query_pool)(VKContext *ctx, int queryType, int queryCount) {
	if (!ctx || !ctx->device) return -1;
	VkQueryPoolCreateInfo qpci = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = (VkQueryType)queryType,
		.queryCount = (uint32_t)queryCount,
	};
	VkQueryPool pool;
	if (fp_vkCreateQueryPool(ctx->device, &qpci, NULL, &pool) != VK_SUCCESS) return -1;
	int id = g_queryCount++;
	if (id >= g_queryCap) { g_queryCap = g_queryCap ? g_queryCap * 2 : 16; g_queries = (VkQueryPool*)realloc(g_queries, g_queryCap * sizeof(VkQueryPool)); }
	g_queries[id] = pool;
	return id + 1;
}

HL_PRIM void HL_NAME(destroy_query_pool)(VKContext *ctx, int qpId) {
	if (!ctx || !ctx->device) return;
	int id = qpId - 1;
	if (id < 0 || id >= g_queryCount) return;
	if (g_queries[id] != VK_NULL_HANDLE) {
		fp_vkDestroyQueryPool(ctx->device, g_queries[id], NULL);
		g_queries[id] = VK_NULL_HANDLE;
	}
}

HL_PRIM void HL_NAME(cmd_begin_query)(VKContext *ctx, int cb, int qpId, int queryIdx, int flags) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	int id = qpId - 1;
	if (id < 0 || id >= g_queryCount || !g_queries[id]) return;
	fp_vkCmdBeginQuery(ctx->commandBuffers[cb], g_queries[id], (uint32_t)queryIdx, (VkQueryControlFlags)flags);
}

HL_PRIM void HL_NAME(cmd_end_query)(VKContext *ctx, int cb, int qpId, int queryIdx) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	int id = qpId - 1;
	if (id < 0 || id >= g_queryCount || !g_queries[id]) return;
	fp_vkCmdEndQuery(ctx->commandBuffers[cb], g_queries[id], (uint32_t)queryIdx);
}

HL_PRIM void HL_NAME(cmd_reset_query_pool)(VKContext *ctx, int cb, int qpId, int firstQuery, int queryCount) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	int id = qpId - 1;
	if (id < 0 || id >= g_queryCount || !g_queries[id]) return;
	fp_vkCmdResetQueryPool(ctx->commandBuffers[cb], g_queries[id], (uint32_t)firstQuery, (uint32_t)queryCount);
}

HL_PRIM bool HL_NAME(get_query_pool_results)(VKContext *ctx, int qpId, int firstQuery, int queryCount, vbyte *data, int dataSize, int stride, int flags) {
	if (!ctx || !ctx->device) return false;
	int id = qpId - 1;
	if (id < 0 || id >= g_queryCount || !g_queries[id]) return false;
	VkResult r = fp_vkGetQueryPoolResults(ctx->device, g_queries[id], (uint32_t)firstQuery, (uint32_t)queryCount, (size_t)dataSize, data, (VkDeviceSize)stride, (VkQueryResultFlags)flags);
	return r == VK_SUCCESS || r == VK_NOT_READY;
}

HL_PRIM void HL_NAME(cmd_pipeline_barrier)(VKContext *ctx, int cb, int srcStage, int dstStage, int srcAccess, int dstAccess, int oldLayout, int newLayout, int imgHandle, int aspect, int baseMip, int mipCount, int baseLayer, int layerCount) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	ImgInfo *ii = get_img(imgHandle);
	if (!ii) return;

	uint32_t sa = (uint32_t)srcAccess;
	uint32_t da = (uint32_t)dstAccess;
	if (sa == 0 && da == 0) {
		if (oldLayout != newLayout) {
			if (oldLayout == 0 && (newLayout == 7 || newLayout == 2))
				da = VK_ACCESS_TRANSFER_WRITE_BIT;
			else if (oldLayout == 7 && newLayout == 5) { sa = VK_ACCESS_TRANSFER_WRITE_BIT; da = VK_ACCESS_SHADER_READ_BIT; }
			else if (newLayout == 5)
				da = VK_ACCESS_SHADER_READ_BIT;
		}
	}

	cmd_image_barrier(ctx, ctx->commandBuffers[cb],
		(uint32_t)srcStage, (uint32_t)dstStage, sa, da,
		(VkImageLayout)oldLayout, (VkImageLayout)newLayout,
		ii->image, (uint32_t)baseMip, (uint32_t)mipCount, (uint32_t)baseLayer, (uint32_t)layerCount);
}

HL_PRIM void HL_NAME(cmd_set_depth_bias)(VKContext *ctx, int cb, float constantFactor, float clamp, float slopeFactor) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	fp_vkCmdSetDepthBias(ctx->commandBuffers[cb], constantFactor, clamp, slopeFactor);
}

HL_PRIM void HL_NAME(cmd_push_constants)(VKContext *ctx, int cb, int layoutHandle, int stageFlags, int offset, int size, vbyte *data) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (!data || size <= 0 || offset < 0) return;
	PlInfo *pli = get_pl(layoutHandle); if (!pli) return;
	float *fdata = (float*)data;
	printf("[VKC] push_constants: cb=%d layout=%d flags=0x%x off=%d size=%d f0=%f f1=%f f2=%f f3=%f\n",
		cb, layoutHandle, stageFlags, offset, size,
		size >= 4 ? fdata[0] : 0.0f,
		size >= 8 ? fdata[1] : 0.0f,
		size >= 12 ? fdata[2] : 0.0f,
		size >= 16 ? fdata[3] : 0.0f);
	if (offset == 0 && size >= 64) {
		printf("[VKC] push_constants[4-7]=%f %f %f %f [8-11]=%f %f %f %f\n",
			fdata[4], fdata[5], fdata[6], fdata[7],
			fdata[8], fdata[9], fdata[10], fdata[11]);
	}

	// Small path: use vkCmdPushConstants directly
	if (size < (int)ctx->maxPushConstantsSize) {
		if ((uint32_t)offset >= ctx->maxPushConstantsSize) return;
		if ((uint32_t)(offset + size) > ctx->maxPushConstantsSize) {
			printf("[VKC] push_constants TRUNCATED: maxPC=%d off=%d size=%d -> size=%d\n", ctx->maxPushConstantsSize, offset, size, (int)(ctx->maxPushConstantsSize - (uint32_t)offset));
			size = (int)(ctx->maxPushConstantsSize - (uint32_t)offset);
		}
		if (size <= 0) return;
		fp_vkCmdPushConstants(ctx->commandBuffers[cb], pli->layout, (VkShaderStageFlags)stageFlags, (uint32_t)offset, (uint32_t)size, data);
		return;
	}

	// Large path: use dynamic UBO fallback
	printf("[VKC] push_constants UBO FALLBACK: size=%d maxPC=%d\n", size, ctx->maxPushConstantsSize);
	if (!ctx->dynamicUBOMapped || ctx->dynamicUBODSId < 0) {
		printf("[VKC] push_constants UBO FAIL: mapped=%p dsId=%d\n", (void*)ctx->dynamicUBOMapped, ctx->dynamicUBODSId);
		return;
	}

	// Align offset to UBO alignment requirement
	int alignedOff = (ctx->dynamicUBOOffset + ctx->dynamicUBOAlignment - 1) & ~(ctx->dynamicUBOAlignment - 1);
	if (alignedOff + size > ctx->dynamicUBOSize)
		alignedOff = 0; // wrap around

	memcpy((char*)ctx->dynamicUBOMapped + alignedOff, data, (size_t)size);

	// Bind dynamic UBO descriptor set at set 3 with dynamic offset
	VkDescriptorSet ds = g_dss[ctx->dynamicUBODSId].ds;
	uint32_t dynOff = (uint32_t)alignedOff;
	fp_vkCmdBindDescriptorSets(ctx->commandBuffers[cb], VK_PIPELINE_BIND_POINT_GRAPHICS,
		pli->layout, 3, 1, &ds, 1, &dynOff);

	ctx->dynamicUBOOffset = alignedOff + size;
}

HL_PRIM int HL_NAME(create_descriptor_set_layout)(VKContext *ctx, int bindingCount, int bindingType) {
	VkDescriptorSetLayoutBinding *bindings = NULL;
	VkDescriptorType descType;
	if (bindingType == 0)
		descType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	else if (bindingType == 2)
		descType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	else
		descType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	VkShaderStageFlags stageFlags = (bindingType == 0)
		? VK_SHADER_STAGE_FRAGMENT_BIT
		: (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	if (bindingCount > 0) {
		bindings = (VkDescriptorSetLayoutBinding *)calloc(bindingCount, sizeof(VkDescriptorSetLayoutBinding));
		for (int i = 0; i < bindingCount; i++) {
			bindings[i].binding = (uint32_t)i;
			bindings[i].descriptorType = descType;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = stageFlags;
		}
	}
	VkDescriptorSetLayoutBindingFlagsCreateInfo bfi = {0};
	if (ctx->hasDescriptorIndexing && !ctx->hasPushDescriptor) {
		bfi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		bfi.bindingCount = (uint32_t)bindingCount;
		VkDescriptorBindingFlags *flags = (VkDescriptorBindingFlags *)calloc(bindingCount, sizeof(VkDescriptorBindingFlags));
		for (int i = 0; i < bindingCount; i++)
			flags[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		bfi.pBindingFlags = flags;
	}
	VkDescriptorSetLayoutCreateFlags dslFlags = 0;
	if (ctx->hasDescriptorIndexing && !ctx->hasPushDescriptor)
		dslFlags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	VkDescriptorSetLayoutCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = (bfi.pBindingFlags != NULL) ? &bfi : NULL,
		.bindingCount = (uint32_t)bindingCount,
		.pBindings = bindings,
		.flags = dslFlags,
	};
	VkDescriptorSetLayout dsl;
	VkResult res = fp_vkCreateDescriptorSetLayout(ctx->device, &ci, NULL, &dsl);
	if (bfi.pBindingFlags) free((void *)bfi.pBindingFlags);
	free(bindings);
	if (res != VK_SUCCESS) return -1;
	if (g_dslCount >= g_dslCap) { g_dslCap = g_dslCap ? g_dslCap * 2 : 8; g_dsls = (VkDescriptorSetLayout*)realloc(g_dsls, g_dslCap * sizeof(VkDescriptorSetLayout)); }
	int id = g_dslCount++; g_dsls[id] = dsl;
	return id;
}

HL_PRIM int HL_NAME(get_descriptor_set_layout_handle)(VKContext *ctx, int dslId) {
	return (dslId >= 0 && dslId < g_dslCount) ? (int)(intptr_t)g_dsls[dslId] : 0;
}

HL_PRIM int HL_NAME(allocate_descriptor_set)(VKContext *ctx, int dslId) {
	if (dslId < 0 || dslId >= g_dslCount || !ctx->descriptorPool) return -1;
	if (g_dsCount >= g_dsCap) { g_dsCap = g_dsCap ? g_dsCap * 2 : 64; g_dss = (DSInfo*)realloc(g_dss, g_dsCap * sizeof(DSInfo)); }
	VkDescriptorSetAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &g_dsls[dslId],
	};
	int id = g_dsCount++;
	VkDescriptorSet ds;
	if (fp_vkAllocateDescriptorSets(ctx->device, &ai, &ds) != VK_SUCCESS) { g_dsCount--; return -1; }
	g_dss[id].ds = ds;
	return id + 1;
}

static DSInfo* get_ds(int i) { return (i > 0 && i <= g_dsCount) ? &g_dss[i - 1] : NULL; }

HL_PRIM void HL_NAME(set_image_sampler)(VKContext *ctx, int imgId, int samplerImgId) {
	ImgInfo *ii = get_img(imgId);
	ImgInfo *si = get_img(samplerImgId);
	if (ii && si) ii->sampler = si->sampler;
}

HL_PRIM void HL_NAME(update_descriptor_set_texture)(VKContext *ctx, int dsHandle, int binding, int samplerId, int imgId) {
	if (!ctx || !ctx->device) return;
	DSInfo *dsi = get_ds(dsHandle);
	if (!dsi || !dsi->ds || (uintptr_t)dsi->ds < 0x1000) { printf("[VKC] update_ds_tex FAIL: ds=%d handle=0x%llx\n", dsHandle, dsi?(unsigned long long)(uintptr_t)dsi->ds:0); return; }
	SamplerInfo *smp = (samplerId > 0 && samplerId <= g_samplerCount) ? &g_samplers[samplerId - 1] : NULL;
	ImgInfo *imi = get_img(imgId);
	if (!smp || !imi || !imi->view || (uintptr_t)imi->view < 0x1000) { printf("[VKC] update_ds_tex FAIL: ds=%d bind=%d smp=%p img=%d view=0x%llx\n", dsHandle, binding, (void*)smp, imgId, imi?(unsigned long long)(uintptr_t)imi->view:0); return; }
	printf("[VKC] update_ds_tex: ds=%d bind=%d sampler=%d img=%d view=0x%llx\n", dsHandle, binding, samplerId, imgId, (unsigned long long)(uintptr_t)imi->view);
	VkDescriptorImageInfo ii = {
		.sampler = smp->sampler,
		.imageView = imi->view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet w = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = dsi->ds,
		.dstBinding = (uint32_t)binding,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo = &ii,
	};
	fp_vkUpdateDescriptorSets(ctx->device, 1, &w, 0, NULL);
}

HL_PRIM void HL_NAME(cmd_bind_descriptor_sets)(VKContext *ctx, int cb, int dsHandle, int firstSet, int layoutHandle) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	DSInfo *dsi = get_ds(dsHandle);
	if (!dsi || !dsi->ds || (uintptr_t)dsi->ds < 0x1000) { printf("[VKC] bind_ds FAIL: ds=%d handle=0x%llx\n", dsHandle, dsi?(unsigned long long)(uintptr_t)dsi->ds:0); return; }
	PlInfo *pli = get_pl(layoutHandle);
	VkPipelineLayout layout = pli ? pli->layout : VK_NULL_HANDLE;
	VkDescriptorSet dsSet = dsi->ds;
	printf("[VKC] bind_ds: cb=%d ds=%d set=%d layout=%d\n", cb, dsHandle, firstSet, layoutHandle);
	fp_vkCmdBindDescriptorSets(ctx->commandBuffers[cb], VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (uint32_t)firstSet, 1, &dsSet, 0, NULL);
}

HL_PRIM void HL_NAME(cmd_bind_compute_pipeline)(VKContext *ctx, int cb, int pipe) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	PipeInfo *pi = get_pipe(pipe);
	if (!pi || !pi->pipeline) return;
	fp_vkCmdBindPipeline(ctx->commandBuffers[cb], VK_PIPELINE_BIND_POINT_COMPUTE, pi->pipeline);
}

HL_PRIM void HL_NAME(cmd_bind_compute_descriptor_sets)(VKContext *ctx, int cb, int dsHandle, int firstSet, int layoutHandle) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	DSInfo *dsi = get_ds(dsHandle);
	if (!dsi || !dsi->ds || (uintptr_t)dsi->ds < 0x1000) return;
	PlInfo *pli = get_pl(layoutHandle);
	VkPipelineLayout layout = pli ? pli->layout : VK_NULL_HANDLE;
	VkDescriptorSet dsSet = dsi->ds;
	fp_vkCmdBindDescriptorSets(ctx->commandBuffers[cb], VK_PIPELINE_BIND_POINT_COMPUTE, layout, (uint32_t)firstSet, 1, &dsSet, 0, NULL);
}

HL_PRIM void HL_NAME(compute_dispatch)(VKContext *ctx, int cb, int groupCountX, int groupCountY, int groupCountZ) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	fp_vkCmdDispatch(ctx->commandBuffers[cb], (uint32_t)groupCountX, (uint32_t)groupCountY, (uint32_t)groupCountZ);
}

HL_PRIM int HL_NAME(create_compute_pipeline)(VKContext *ctx, int cs, int layout) {
	ShdInfo *csi = get_shd(cs); if (!csi) return -1;
	PlInfo *pli = get_pl(layout); if (!pli) return -1;

	VkPipelineShaderStageCreateInfo stage = {0};
	stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = csi->module;
	stage.pName = "main";

	VkComputePipelineCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stage,
		.layout = pli->layout,
	};

	VkPipeline p;
	if (fp_vkCreateComputePipelines(ctx->device, ctx->pipelineCache, 1, &cpci, NULL, &p) != VK_SUCCESS) return -1;
	return add_pipe(p);
}

HL_PRIM void HL_NAME(update_descriptor_set_buffer)(VKContext *ctx, int dsHandle, int binding, int bufId, int offset, int range) {
	DSInfo *dsi = get_ds(dsHandle);
	if (!dsi || bufId < 0) return;
	BufInfo *bi = get_buf(bufId);
	if (!bi) return;
	VkDescriptorBufferInfo dbi = {
		.buffer = bi->buffer,
		.offset = (VkDeviceSize)offset,
		.range = (range > 0 ? (VkDeviceSize)range : VK_WHOLE_SIZE),
	};
	VkWriteDescriptorSet w = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = (VkDescriptorSet)(intptr_t)dsHandle,
		.dstBinding = (uint32_t)binding,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo = &dbi,
	};
	fp_vkUpdateDescriptorSets(ctx->device, 1, &w, 0, NULL);
}

HL_PRIM void HL_NAME(update_descriptor_set_uniform)(VKContext *ctx, int dsHandle, int binding, int bufId, int offset, int range) {
	DSInfo *dsi = get_ds(dsHandle);
	if (!dsi || bufId < 0) { printf("[VKC] update_ds_ubo FAIL: ds=%d bufId=%d\n", dsHandle, bufId); return; }
	BufInfo *bi = get_buf(bufId);
	if (!bi) { printf("[VKC] update_ds_ubo FAIL: buf %d not found\n", bufId); return; }
	printf("[VKC] update_ds_ubo: ds=%d bind=%d buf=%d off=%d range=%d\n", dsHandle, binding, bufId, offset, range);
	VkDescriptorBufferInfo dbi = {
		.buffer = bi->buffer,
		.offset = (VkDeviceSize)offset,
		.range = (range > 0 ? (VkDeviceSize)range : VK_WHOLE_SIZE),
	};
	VkWriteDescriptorSet w = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = dsi->ds,
		.dstBinding = (uint32_t)binding,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo = &dbi,
	};
	fp_vkUpdateDescriptorSets(ctx->device, 1, &w, 0, NULL);
}

HL_PRIM void HL_NAME(destroy_descriptor_set_layout)(VKContext *ctx, int dslId) {
	if (dslId < 0 || dslId >= g_dslCount) return;
	fp_vkDestroyDescriptorSetLayout(ctx->device, g_dsls[dslId], NULL);
	g_dsls[dslId] = VK_NULL_HANDLE;
}

static int add_img(VkImage img, VkDeviceMemory mem, VkImageView view, VkSampler sampler, int w, int h, int fmt, int mips, int layers) {
	if (g_imgCount >= g_imgCap) { g_imgCap = g_imgCap ? g_imgCap * 2 : 32; g_imgs = (ImgInfo*)realloc(g_imgs, g_imgCap * sizeof(ImgInfo)); }
	int id = g_imgCount++; ImgInfo *ii = &g_imgs[id];
	ii->image = img; ii->memory = mem; ii->view = view; ii->sampler = sampler;
	ii->width = w; ii->height = h; ii->format = fmt; ii->mipLevels = mips; ii->layerCount = layers;
	ii->lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	return id;
}
static ImgInfo* get_img(int i) { return (i >= 0 && i < g_imgCount) ? &g_imgs[i] : NULL; }

HL_PRIM int HL_NAME(create_image)(VKContext *ctx, int w, int h, int format, int mipLevels, int layerCount, int usage) {
	VkImageCreateInfo ii = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = (VkFormat)format,
		.extent = { (uint32_t)w, (uint32_t)h, 1 },
		.mipLevels = (uint32_t)mipLevels,
		.arrayLayers = (uint32_t)layerCount,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = (VkImageUsageFlags)usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VkImage img;
	if (fp_vkCreateImage(ctx->device, &ii, NULL, &img) != VK_SUCCESS) return -1;

	VkMemoryRequirements mr;
	fp_vkGetImageMemoryRequirements(ctx->device, img, &mr);
	VkMemoryAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mr.size,
		.memoryTypeIndex = find_mem_type(ctx->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	};
	VkDeviceMemory mem;
	if (fp_vkAllocateMemory(ctx->device, &ai, NULL, &mem) != VK_SUCCESS) {
		fp_vkDestroyImage(ctx->device, img, NULL);
		return -1;
	}
	fp_vkBindImageMemory(ctx->device, img, mem, 0);
	return add_img(img, mem, VK_NULL_HANDLE, VK_NULL_HANDLE, w, h, format, mipLevels, layerCount);
}

HL_PRIM int HL_NAME(create_image_view)(VKContext *ctx, int imgId, int format, int aspect, int baseMip, int mipCount, int baseLayer, int layerCount) {
	ImgInfo *ii = get_img(imgId);
	if (!ii) return 0;
	VkComponentMapping components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
	if (format == 9 || format == 1000054000) {
		components.r = VK_COMPONENT_SWIZZLE_R;
		components.g = VK_COMPONENT_SWIZZLE_R;
		components.b = VK_COMPONENT_SWIZZLE_R;
		components.a = VK_COMPONENT_SWIZZLE_R;
	}
	VkImageViewCreateInfo vi = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = ii->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = (VkFormat)format,
		.components = components,
		.subresourceRange = {
			.aspectMask = (VkImageAspectFlags)aspect,
			.baseMipLevel = (uint32_t)baseMip,
			.levelCount = (uint32_t)mipCount,
			.baseArrayLayer = (uint32_t)baseLayer,
			.layerCount = (uint32_t)layerCount,
		},
	};
	VkImageView view;
	if (fp_vkCreateImageView(ctx->device, &vi, NULL, &view) != VK_SUCCESS) return 0;
	ii->view = view;
	return (int)(intptr_t)view;
}

HL_PRIM int HL_NAME(create_sampler)(VKContext *ctx, int filter, int mipMode, int addrMode, bool anisotropy, float maxAnisotropy) {
	VkSamplerCreateInfo si = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = (VkFilter)filter,
		.minFilter = (VkFilter)filter,
		.mipmapMode = (VkSamplerMipmapMode)mipMode,
		.addressModeU = (VkSamplerAddressMode)addrMode,
		.addressModeV = (VkSamplerAddressMode)addrMode,
		.addressModeW = (VkSamplerAddressMode)addrMode,
		.anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE,
		.maxAnisotropy = anisotropy ? (maxAnisotropy > 0 ? maxAnisotropy : 16.0f) : 1.0f,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.minLod = 0.0f,
		.maxLod = VK_LOD_CLAMP_NONE,
	};
	if (g_samplerCount >= g_samplerCap) { g_samplerCap = g_samplerCap ? g_samplerCap * 2 : 32; g_samplers = (SamplerInfo*)realloc(g_samplers, g_samplerCap * sizeof(SamplerInfo)); }
	VkSampler sampler;
	if (fp_vkCreateSampler(ctx->device, &si, NULL, &sampler) != VK_SUCCESS) return -1;
	int id = g_samplerCount++;
	g_samplers[id].sampler = sampler;
	return id + 1;
}

HL_PRIM void HL_NAME(destroy_image)(VKContext *ctx, int imgId, int memId) {
	ImgInfo *ii = get_img(imgId);
	if (!ii) return;
	if (ii->view != VK_NULL_HANDLE) fp_vkDestroyImageView(ctx->device, ii->view, NULL);
	if (ii->sampler != VK_NULL_HANDLE) fp_vkDestroySampler(ctx->device, ii->sampler, NULL);
	if (ii->image != VK_NULL_HANDLE) fp_vkDestroyImage(ctx->device, ii->image, NULL);
	if (ii->memory != VK_NULL_HANDLE) fp_vkFreeMemory(ctx->device, ii->memory, NULL);
	memset(ii, 0, sizeof(ImgInfo));
}

HL_PRIM void HL_NAME(destroy_image_view)(VKContext *ctx, int viewHandle) {
	if (viewHandle == 0) return;
	fp_vkDestroyImageView(ctx->device, (VkImageView)(intptr_t)viewHandle, NULL);
}

HL_PRIM void HL_NAME(destroy_sampler)(VKContext *ctx, int samplerHandle) {
	int id = samplerHandle - 1;
	if (id < 0 || id >= g_samplerCount) return;
	SamplerInfo *si = &g_samplers[id];
	if (si->sampler != VK_NULL_HANDLE) {
		fp_vkDestroySampler(ctx->device, si->sampler, NULL);
		si->sampler = VK_NULL_HANDLE;
	}
}

HL_PRIM int HL_NAME(get_image_handle)(VKContext *ctx, int imgId) {
	ImgInfo *ii = get_img(imgId);
	return ii ? (int)(intptr_t)ii->image : 0;
}

HL_PRIM int HL_NAME(get_image_width)(VKContext *ctx, int imgId) {
	ImgInfo *ii = get_img(imgId);
	return ii ? ii->width : 0;
}

HL_PRIM int HL_NAME(get_image_height)(VKContext *ctx, int imgId) {
	ImgInfo *ii = get_img(imgId);
	return ii ? ii->height : 0;
}

HL_PRIM void HL_NAME(cmd_blit_image)(VKContext *ctx, int cb, int srcImgId, int dstImgId, int srcMip, int dstMip) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	ImgInfo *src = get_img(srcImgId);
	ImgInfo *dst = get_img(dstImgId);
	if (!src || !dst) return;

	VkImageBlit blit = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)srcMip, 0, 1 },
		.srcOffsets = { {0,0,0}, { src->width >> srcMip > 0 ? (src->width >> srcMip) : 1, src->height >> srcMip > 0 ? (src->height >> srcMip) : 1, 1 } },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)dstMip, 0, 1 },
		.dstOffsets = { {0,0,0}, { dst->width >> dstMip > 0 ? (dst->width >> dstMip) : 1, dst->height >> dstMip > 0 ? (dst->height >> dstMip) : 1, 1 } },
	};
	fp_vkCmdBlitImage(ctx->commandBuffers[cb], src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
}

HL_PRIM void HL_NAME(cmd_resolve_image)(VKContext *ctx, int cb, int srcImgId, int dstImgId, int srcMip, int dstMip, int srcLayer, int dstLayer) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	ImgInfo *src = get_img(srcImgId);
	ImgInfo *dst = get_img(dstImgId);
	if (!src || !dst) return;
	VkImageResolve resolve = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)srcMip, (uint32_t)srcLayer, 1 },
		.srcOffset = {0, 0, 0},
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)dstMip, (uint32_t)dstLayer, 1 },
		.dstOffset = {0, 0, 0},
		.extent = { (uint32_t)(src->width >> srcMip > 0 ? (src->width >> srcMip) : 1), (uint32_t)(src->height >> srcMip > 0 ? (src->height >> srcMip) : 1), 1 },
	};
	fp_vkCmdResolveImage(ctx->commandBuffers[cb], src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &resolve);
}

HL_PRIM void HL_NAME(cmd_copy_buffer_to_image)(VKContext *ctx, int cb, int srcBufId, int dstImgId, int mipLevel, int arrayLayer) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	BufInfo *bi = get_buf(srcBufId);
	ImgInfo *ii = get_img(dstImgId);
	if (!bi || !ii) return;

	VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)mipLevel, (uint32_t)arrayLayer, 1 },
		.imageOffset = {0, 0, 0},
		.imageExtent = { (uint32_t)(ii->width >> mipLevel > 0 ? (ii->width >> mipLevel) : 1), (uint32_t)(ii->height >> mipLevel > 0 ? (ii->height >> mipLevel) : 1), 1 },
	};
	fp_vkCmdCopyBufferToImage(ctx->commandBuffers[cb], bi->buffer, ii->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

HL_PRIM void HL_NAME(cmd_copy_buffer)(VKContext *ctx, int cb, int srcBufId, int dstBufId, int size) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	BufInfo *src = get_buf(srcBufId);
	BufInfo *dst = get_buf(dstBufId);
	if (!src || !dst) return;
	VkBufferCopy region = { .size = (VkDeviceSize)size };
	fp_vkCmdCopyBuffer(ctx->commandBuffers[cb], src->buffer, dst->buffer, 1, &region);
}

HL_PRIM void HL_NAME(cmd_fill_buffer)(VKContext *ctx, int cb, int bufId, int dstOffset, int size, int data) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	BufInfo *bi = get_buf(bufId);
	if (!bi) return;
	fp_vkCmdFillBuffer(ctx->commandBuffers[cb], bi->buffer, (VkDeviceSize)dstOffset, (VkDeviceSize)size, (uint32_t)data);
}

HL_PRIM void HL_NAME(cmd_update_buffer)(VKContext *ctx, int cb, int bufId, int offset, vbyte *data, int size) {
	if (!ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	if (size > 65536) return;
	BufInfo *bi = get_buf(bufId);
	if (!bi) return;
	fp_vkCmdUpdateBuffer(ctx->commandBuffers[cb], bi->buffer, (VkDeviceSize)offset, (VkDeviceSize)size, data);
}

HL_PRIM void HL_NAME(cmd_write_timestamp)(VKContext *ctx, int cb, int qpId, int queryIdx, int stage) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	int id = qpId - 1;
	if (id < 0 || id >= g_queryCount || !g_queries[id]) return;
	fp_vkCmdWriteTimestamp(ctx->commandBuffers[cb], (VkPipelineStageFlagBits)stage, g_queries[id], (uint32_t)queryIdx);
}

HL_PRIM void HL_NAME(cmd_clear_attachments)(VKContext *ctx, int cb, int attachmentCount, float cr, float cg, float cb2, float ca) {
	if (!ctx || !ctx->commandBuffers || (uint32_t)cb >= ctx->commandBufferCount) return;
	VkClearAttachment att;
	att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	att.colorAttachment = 0;
	att.clearValue.color.float32[0] = cr;
	att.clearValue.color.float32[1] = cg;
	att.clearValue.color.float32[2] = cb2;
	att.clearValue.color.float32[3] = ca;
	VkClearRect rect = { .rect = {{0,0},{(uint32_t)ctx->width,(uint32_t)ctx->height}}, .baseArrayLayer = 0, .layerCount = 1 };
	fp_vkCmdClearAttachments(ctx->commandBuffers[cb], 1, &att, 1, &rect);
}

HL_PRIM uint64 HL_NAME(get_buffer_device_address)(VKContext *ctx, int bufId) {
	if (!ctx || !ctx->device) return 0;
	BufInfo *bi = get_buf(bufId);
	if (!bi || !bi->buffer) return 0;
	if (!fp_vkGetBufferDeviceAddress) return 0;
	VkBufferDeviceAddressInfo bdai = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = bi->buffer };
	return fp_vkGetBufferDeviceAddress(ctx->device, &bdai);
}

#ifdef HAS_SHADERC
#include <shaderc/shaderc.h>
HL_PRIM int HL_NAME(compile_glsl_to_spv)(vbyte *glsl, bool isFrag, vbyte *outPath) {
	if (!glsl || !outPath) return 0;
	shaderc_compiler_t compiler = shaderc_compiler_initialize();
	if (!compiler) return 0;
	shaderc_compile_options_t options = shaderc_compile_options_initialize();
	shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
	shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
	shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
	shaderc_shader_kind kind = isFrag ? shaderc_fragment_shader : shaderc_vertex_shader;
	shaderc_compilation_result_t result = shaderc_compile_into_spv(
		compiler, (const char*)glsl, strlen((const char*)glsl), kind, "shader", "main", options);
	int ok = 0;
	if (shaderc_result_get_compilation_status(result) == shaderc_compilation_status_success) {
		size_t len = shaderc_result_get_length(result);
		const char* bytes = shaderc_result_get_bytes(result);
		FILE *f = fopen((const char*)outPath, "wb");
		if (f) { fwrite(bytes, 1, len, f); fclose(f); ok = 1; }
	}
	shaderc_result_release(result);
	shaderc_compile_options_release(options);
	shaderc_compiler_release(compiler);
	return ok;
}
#else
HL_PRIM int HL_NAME(compile_glsl_to_spv)(vbyte *glsl, bool isFrag, vbyte *outPath) {
	return 0;
}
#endif

DEFINE_PRIM(_I32, create_buffer, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, destroy_buffer, TVK_CTX _I32);
DEFINE_PRIM(_VOID, upload_buffer, TVK_CTX _I32 _BYTES _I32 _I32);
DEFINE_PRIM(_VOID, upload_buffer_staging, TVK_CTX _I32 _BYTES _I32 _I32);
DEFINE_PRIM(_VOID, upload_buffer_floats, TVK_CTX _I32 _DYN _I32 _I32 _I32);
DEFINE_PRIM(_VOID, upload_buffer_shorts, TVK_CTX _I32 _DYN _I32 _I32 _I32);
DEFINE_PRIM(_I32, get_buffer_handle, TVK_CTX _I32);
DEFINE_PRIM(_I32, create_shader, TVK_CTX _BYTES _I32);
DEFINE_PRIM(_VOID, destroy_shader, TVK_CTX _I32);
DEFINE_PRIM(_I32, create_pipeline_layout, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, destroy_pipeline_layout, TVK_CTX _I32);
DEFINE_PRIM(_VOID, set_vertex_input_state, TVK_CTX _I32 _I32 _BYTES);
DEFINE_PRIM(_I32, create_graphics_pipeline, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_I32, create_graphics_pipeline_dynamic, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_I32, create_graphics_pipeline_dynamic_vi, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _BYTES);
DEFINE_PRIM(_I32, create_graphics_pipeline_2d, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, destroy_pipeline, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_bind_pipeline, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_bind_vertex_buffer, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_bind_index_buffer, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_viewport, TVK_CTX _I32 _F32 _F32 _F32 _F32);
DEFINE_PRIM(_VOID, cmd_set_scissor, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_draw, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_draw_indexed, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_pipeline_barrier, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_depth_bias, TVK_CTX _I32 _F32 _F32 _F32);
DEFINE_PRIM(_I32, create_image, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_I32, create_image_view, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_I32, create_sampler, TVK_CTX _I32 _I32 _I32 _BOOL _F32);
DEFINE_PRIM(_VOID, destroy_image, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, destroy_image_view, TVK_CTX _I32);
DEFINE_PRIM(_VOID, destroy_sampler, TVK_CTX _I32);
DEFINE_PRIM(_I32, get_image_handle, TVK_CTX _I32);
DEFINE_PRIM(_I32, get_image_width, TVK_CTX _I32);
DEFINE_PRIM(_I32, get_image_height, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_push_constants, TVK_CTX _I32 _I32 _I32 _I32 _I32 _BYTES);
DEFINE_PRIM(_I32, create_descriptor_set_layout, TVK_CTX _I32 _I32);
DEFINE_PRIM(_I32, get_descriptor_set_layout_handle, TVK_CTX _I32);
DEFINE_PRIM(_I32, allocate_descriptor_set, TVK_CTX _I32);
DEFINE_PRIM(_VOID, update_descriptor_set_texture, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, update_descriptor_set_buffer, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, update_descriptor_set_uniform, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_bind_descriptor_sets, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, destroy_descriptor_set_layout, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_blit_image, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_resolve_image, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_copy_buffer_to_image, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_draw_indexed_indirect, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_I32, create_query_pool, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, destroy_query_pool, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_begin_query, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_end_query, TVK_CTX _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_reset_query_pool, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_BOOL, get_query_pool_results, TVK_CTX _I32 _I32 _I32 _BYTES _I32 _I32 _I32);
DEFINE_PRIM(_I32, create_compute_pipeline, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_bind_compute_pipeline, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_bind_compute_descriptor_sets, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, compute_dispatch, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_draw_indirect_count, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_copy_buffer, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_fill_buffer, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_update_buffer, TVK_CTX _I32 _I32 _I32 _BYTES _I32);
DEFINE_PRIM(_VOID, cmd_write_timestamp, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_clear_attachments, TVK_CTX _I32 _I32 _F32 _F32 _F32 _F32);
DEFINE_PRIM(_I64, get_buffer_device_address, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_set_line_width, TVK_CTX _I32 _F32);
DEFINE_PRIM(_VOID, cmd_set_stencil_reference, TVK_CTX _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_cull_mode, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_front_face, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_primitive_topology, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_depth_test_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_depth_write_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_depth_compare_op, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_stencil_test_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_depth_bias_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_rasterizer_discard_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_BOOL, check_descriptor_set_layout_support, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, host_reset_query_pool, TVK_CTX _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_push_constants2, TVK_CTX _I32 _I32 _I32 _I32 _I32 _BYTES);
DEFINE_PRIM(_VOID, cmd_copy_image, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_copy_image_to_buffer, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_clear_depth_stencil, TVK_CTX _I32 _I32 _I32 _F32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_blend_constants, TVK_CTX _I32 _F32 _F32 _F32 _F32);
DEFINE_PRIM(_I32, create_event, TVK_CTX);
DEFINE_PRIM(_VOID, destroy_event, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_set_event, TVK_CTX _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_wait_events, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, flush_memory, TVK_CTX _I32);
DEFINE_PRIM(_VOID, invalidate_memory, TVK_CTX _I32);
DEFINE_PRIM(_VOID, bind_image_memory2, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_polygon_mode, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_primitive_restart_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_rasterization_samples, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_draw_indirect_byte_count, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_logic_op_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_logic_op, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_color_blend_enable, TVK_CTX _I32 _I32 _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_color_blend_equation, TVK_CTX _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_color_write_mask, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_depth_clamp_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_provoking_vertex_mode, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_line_rasterization_mode, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_tessellation_domain_origin, TVK_CTX _I32 _I32);
DEFINE_PRIM(_VOID, cmd_copy_buffer2, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_copy_image2, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_blit_image2, TVK_CTX _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, set_debug_name, TVK_CTX _I32 _I32 _BYTES);
DEFINE_PRIM(_VOID, cmd_begin_debug_label, TVK_CTX _I32 _F32 _F32 _F32 _F32 _BYTES);
DEFINE_PRIM(_VOID, cmd_end_debug_label, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_reset_event, TVK_CTX _I32 _I32 _I32);
DEFINE_PRIM(_BOOL, get_event_status, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_begin_conditional_rendering, TVK_CTX _I32 _I32 _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_end_conditional_rendering, TVK_CTX _I32);
DEFINE_PRIM(_VOID, cmd_set_alpha_to_one_enable, TVK_CTX _I32 _BOOL);
DEFINE_PRIM(_VOID, cmd_set_fragment_shading_rate, TVK_CTX _I32 _I32 _I32);
DEFINE_PRIM(_VOID, cmd_set_sample_locations, TVK_CTX _I32 _I32);
DEFINE_PRIM(_I32, create_descriptor_update_template, TVK_CTX _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, destroy_descriptor_update_template, TVK_CTX _I32);
DEFINE_PRIM(_I32, compile_glsl_to_spv, _BYTES _BOOL _BYTES);