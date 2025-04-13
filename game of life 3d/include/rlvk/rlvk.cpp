#include "rlvk.hpp"
#include "gtc/matrix_transform.hpp"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <bitset>

#include "solid.h"
#include "wire.h"
#include "cube.h"

#define VK_NO_PROTOTYPES
#include "vulkan.h"
#include "volk.h"
#include "glfw3.h"
#include "vk_format_utils.h"

static constexpr int gFramesInFlight = 2;

static struct Image {
	VkDeviceMemory memory = {};
	VkImage image = {};
	VkImageView view = {};
};

struct Buffer {
	VkDeviceMemory memory = {};
	VkBuffer buffer = {};
	union {
		void* hostPtr = nullptr;
		VkDeviceAddress devicePtr;
	};
};

static struct PushConstants {
	VkDeviceAddress buf;
	uint32_t offs;
	glm::mat4 trans;
};

static struct Cube {
	glm::vec3 pos;
	glm::vec3 size;
	glm::u8vec4 color;
};

static struct Globals {
	// windowing
	int width, height;
	GLFWwindow* win;
	unsigned int windowFlags;

	// timing
	double frameTime;
	double curTime;

	//input
	double scroll;
	glm::dvec2 prevMousePos;
	glm::dvec2 mousePos;
	std::bitset<KEY_KP_EQUAL + 1> prevKeys;
	std::bitset<KEY_KP_EQUAL + 1> keys;

	// vulkan
	VkInstance inst;
	VkPhysicalDevice pDev;
	VkPhysicalDeviceMemoryProperties mProps;
	VkDevice lDev;
	uint32_t fam;
	VkQueue q;
	VkSurfaceKHR surf;
	VkSurfaceFormatKHR surfformat;
	VkPresentModeKHR mode;
	VkSwapchainKHR swap;
	std::vector<VkImage> images;
	std::vector<VkImageView> views;
	Image msaa;
	Image ds;
	uint32_t img;
	VkPipelineLayout layout;
	VkPipeline wirePipe;
	VkPipeline solidPipe;

	struct {
		VkCommandPool cmdPool;
		VkCommandBuffer cmdBuffer;
		VkSemaphore acquireSem;
		VkSemaphore presentSem;
		VkFence fence;
	} perFrame[gFramesInFlight];
	uint64_t idx;

	std::vector<Cube> solids;
	std::vector<Cube> wires;

	Buffer staging[gFramesInFlight];
	Buffer cubes;

	VkClearColorValue col;

	glm::mat4 transform;
} g = { 0 };

void SetConfigFlags(unsigned int flags) {
	g.windowFlags = flags;
}

void SetTargetFPS(int fps) {
	g.frameTime = 1.0 / fps;
}

inline glm::mat4 perspective(float fovy, float aspect, float zNear) {
	float f = 1.0f / tanf(fovy * 0.5f);
	return glm::mat4(
		f / aspect, 0.0f, 0.0f, 0.0f,
		0.0f, -f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, -1.0f,
		0.0f, 0.0f, zNear, 0.0f
	);
}

static uint32_t getMemoryIndex(VkMemoryPropertyFlags flags, uint32_t mask) {
	for(uint32_t idx = 0; idx < g.mProps.memoryTypeCount; idx++) {
		if(((1 << idx) & mask) && (g.mProps.memoryTypes[idx].propertyFlags & flags) == flags) {
			return idx;
		}
	}
}

static Image createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, bool msaa) {
	Image image;
	VkImageCreateInfo ici = {};
	ici.imageType = VK_IMAGE_TYPE_2D,
		ici.format = format,
		ici.extent = { width, height, 1 },
		ici.mipLevels = 1,
		ici.arrayLayers = 1,
		ici.samples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
		ici.usage = usage,
		ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vkCreateImage(g.lDev, &ici, nullptr, &image.image);

	VkMemoryRequirements mrq;
	vkGetImageMemoryRequirements(g.lDev, image.image, &mrq);

	VkMemoryAllocateInfo ai = {};
	ai.allocationSize = mrq.size;
	ai.memoryTypeIndex = getMemoryIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mrq.memoryTypeBits);
	vkGetImageMemoryRequirements(g.lDev, image.image, &mrq);
	vkAllocateMemory(g.lDev, &ai, nullptr, &image.memory);
	vkBindImageMemory(g.lDev, image.image, image.memory, 0);

	VkImageViewCreateInfo vci = {};
	vci.image = image.image;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = format;
	vci.subresourceRange = { static_cast<VkImageAspectFlags>((format < 124 || format > 130) ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT), 0, 1, 0, 1 };
	vkCreateImageView(g.lDev, &vci, nullptr, &image.view);

	return image;
}

static void destroyImage(Image image) {
	vkDestroyImageView(g.lDev, image.view, nullptr);
	vkDestroyImage(g.lDev, image.image, nullptr);
	vkFreeMemory(g.lDev, image.memory, nullptr);
}

Buffer createBuffer(uint64_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps) {
	Buffer buffer;

	VkBufferCreateInfo ci = {};
	ci.size = size;
	ci.usage = usage;
	ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vkCreateBuffer(g.lDev, &ci, nullptr, &buffer.buffer);

	VkMemoryRequirements mrq;
	vkGetBufferMemoryRequirements(g.lDev, buffer.buffer, &mrq);

	VkMemoryAllocateFlagsInfo af = {};
	af.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

	VkMemoryAllocateInfo ai = {};
	ai.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &af : nullptr;
	ai.allocationSize = mrq.size;
	ai.memoryTypeIndex = getMemoryIndex(memProps, mrq.memoryTypeBits);
	vkAllocateMemory(g.lDev, &ai, nullptr, &buffer.memory);
	vkBindBufferMemory(g.lDev, buffer.buffer, buffer.memory, 0);

	if(memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		vkMapMemory(g.lDev, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.hostPtr);
	}
	else if(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		VkBufferDeviceAddressInfo da = {};
		da.buffer = buffer.buffer;
		buffer.devicePtr = vkGetBufferDeviceAddress(g.lDev, &da);
	}

	return buffer;
}

void destroyBuffer(Buffer buffer) {
	vkDestroyBuffer(g.lDev, buffer.buffer, nullptr);
	vkFreeMemory(g.lDev, buffer.memory, nullptr);
}

static void createSwapchain() {
	bool msaa = g.windowFlags & FLAG_MSAA_4X_HINT;

	VkSwapchainKHR oldSwapchain = g.swap;
	VkSwapchainCreateInfoKHR ci = {};
	ci.surface = g.surf;
	ci.minImageCount = 3;
	ci.imageFormat = g.surfformat.format;
	ci.imageColorSpace = g.surfformat.colorSpace;
	ci.imageExtent = { static_cast<uint32_t>(g.width), static_cast<uint32_t>(g.height) };
	ci.imageArrayLayers = 1;
	ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ci.queueFamilyIndexCount = 1;
	ci.pQueueFamilyIndices = &g.fam;
	ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	ci.presentMode = g.mode;
	ci.clipped = true;
	ci.oldSwapchain = oldSwapchain;
	vkCreateSwapchainKHR(g.lDev, &ci, nullptr, &g.swap);
	vkDestroySwapchainKHR(g.lDev, oldSwapchain, nullptr);

	uint32_t numSwapchainImages;
	vkGetSwapchainImagesKHR(g.lDev, g.swap, &numSwapchainImages, nullptr);

	g.images.resize(numSwapchainImages);
	vkGetSwapchainImagesKHR(g.lDev, g.swap, &numSwapchainImages, g.images.data());

	for(VkImage img : g.images) {
		VkImageView cur;
		VkImageViewCreateInfo ci = {};
		ci.image = img;
		ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ci.format = g.surfformat.format;
		ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCreateImageView(g.lDev, &ci, nullptr, &cur);
		g.views.push_back(cur);
	}

	if(msaa) {
		g.msaa = createImage(g.width, g.height, g.surfformat.format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, true);
	}
	g.ds = createImage(g.width, g.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, msaa);
}

static void recreateSwapchain() {
	glfwGetFramebufferSize(g.win, &g.width, &g.height);
	while(g.width == 0 || g.height == 0) {
		glfwGetFramebufferSize(g.win, &g.width, &g.height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(g.lDev);

	for(VkImageView view : g.views) {
		vkDestroyImageView(g.lDev, view, nullptr);
	}
	g.views.resize(0);

	destroyImage(g.msaa);
	destroyImage(g.ds);

	createSwapchain();
}

void InitWindow(int width, int height, const char* title) {
	// glfw
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
		g.win = glfwCreateWindow(width, height, title, nullptr, nullptr);
		g.width = width;
		g.height = height;

		glfwSetKeyCallback(g.win, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
			g.keys[key] = action == GLFW_PRESS;
			});
		glfwSetMouseButtonCallback(g.win, [](GLFWwindow* window, int key, int action, int mods) {
			g.keys[key] = action == GLFW_PRESS;
			});
		glfwSetCursorPosCallback(g.win, [](GLFWwindow* window, double x, double y) {
			g.mousePos = { x, y };
			});
		glfwSetScrollCallback(g.win, [](GLFWwindow* window, double xOffset, double yOffset) {
			g.scroll = yOffset;
			});
	}

	// VkInstance
	{
		volkInitialize();

		unsigned int glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		VkApplicationInfo ai = {};
		ai.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo ci = {};
		ci.pApplicationInfo = &ai;
		ci.enabledExtensionCount = glfwExtensionCount;
		ci.ppEnabledExtensionNames = glfwExtensions;

		vkCreateInstance(&ci, nullptr, &g.inst);

		volkLoadInstanceOnly(g.inst);
	}

	// VkPhysicalDevice
	{
		uint32_t one = 1;
		vkEnumeratePhysicalDevices(g.inst, &one, &g.pDev);
		vkGetPhysicalDeviceMemoryProperties(g.pDev, &g.mProps);
	}

	// VkDevice and VkQueue
	{
		uint32_t size = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(g.pDev, &size, nullptr);

		std::vector<VkQueueFamilyProperties> queueProperties(size);
		vkGetPhysicalDeviceQueueFamilyProperties(g.pDev, &size, queueProperties.data());

		for(int idx = 0; idx < queueProperties.size(); idx++) {
			if(queueProperties[idx].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				g.fam = idx;
				break;
			}
		}

		float one = 1.0f;
		VkDeviceQueueCreateInfo qi = {};
		qi.queueFamilyIndex = g.fam;
		qi.queueCount = 1;
		qi.pQueuePriorities = &one;

		VkPhysicalDeviceVulkan12Features f12 = {};
		VkPhysicalDeviceVulkan13Features f13 = {};

		f13.dynamicRendering = true;
		f13.synchronization2 = true;

		f12.pNext = &f13;
		f12.shaderInt8 = true;
		f12.scalarBlockLayout = true;
		f12.bufferDeviceAddress = true;
		f12.storageBuffer8BitAccess = true;

		const char* swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		VkDeviceCreateInfo ci = {};
		ci.pNext = &f12;
		ci.queueCreateInfoCount = 1;
		ci.pQueueCreateInfos = &qi;
		ci.enabledExtensionCount = 1;
		ci.ppEnabledExtensionNames = &swapchain;


		vkCreateDevice(g.pDev, &ci, nullptr, &g.lDev);

		volkLoadDevice(g.lDev);
		vkGetDeviceQueue(g.lDev, g.fam, 0, &g.q);
	}

	// per-frame data (vk::CommandPool, vk::CommandBuffer, vk::Semaphores, vk::Fence)
	{
		for(int i = 0; i < gFramesInFlight; i++) {
			VkCommandPoolCreateInfo pci = {};
			pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
			pci.queueFamilyIndex = g.fam;
			vkCreateCommandPool(g.lDev, &pci, nullptr, &g.perFrame[i].cmdPool);

			VkCommandBufferAllocateInfo ai = {};
			ai.commandPool = g.perFrame[i].cmdPool;
			ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			ai.commandBufferCount = 1;
			vkAllocateCommandBuffers(g.lDev, &ai, &g.perFrame[i].cmdBuffer);

			VkSemaphoreCreateInfo sci = {};
			vkCreateSemaphore(g.lDev, &sci, nullptr, &g.perFrame[i].acquireSem);
			vkCreateSemaphore(g.lDev, &sci, nullptr, &g.perFrame[i].presentSem);

			VkFenceCreateInfo fci = {};
			fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			vkCreateFence(g.lDev, &fci, nullptr, &g.perFrame[i].fence);
		}
	}

	// VkSurface and VkSwapchain
	{
		uint32_t count = 0;
		glfwCreateWindowSurface(g.inst, g.win, nullptr, &g.surf);
		vkGetPhysicalDeviceSurfaceFormatsKHR(g.pDev, g.surf, &count, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(g.pDev, g.surf, &count, formats.data());

		g.surfformat = formats.front();
		for(VkSurfaceFormatKHR format : formats) {
			if(!vkuFormatIsSRGB(format.format)) {
				g.surfformat = format;
				break;
			}
		}

		count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(g.pDev, g.surf, &count, nullptr);
		std::vector<VkPresentModeKHR> modes(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(g.pDev, g.surf, &count, modes.data());

		g.mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		for(auto mode : modes) {
			if(mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				g.mode == VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			}
		}

		createSwapchain();
	}

	// VkPipelineLayout
	{

		VkPushConstantRange range = {};
		range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		range.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo ci = {};
		ci.pushConstantRangeCount = 1;
		ci.pPushConstantRanges = &range;
		vkCreatePipelineLayout(g.lDev, &ci, nullptr, &g.layout);
	}

	// VkPipelines
	{
		VkPipelineRenderingCreateInfo ri = {};
		ri.colorAttachmentCount = 1;
		ri.pColorAttachmentFormats = &g.surfformat.format;
		ri.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;


		VkShaderModuleCreateInfo vtxi = {};
		vtxi.codeSize = wire_vert_size * sizeof(uint32_t);
		vtxi.pCode = wire_vert;

		VkShaderModule vtxModule;
		vkCreateShaderModule(g.lDev, &vtxi, nullptr, &vtxModule);

		VkShaderModuleCreateInfo frgi = {};
		frgi.codeSize = cube_frag_size * sizeof(uint32_t);
		frgi.pCode = cube_frag;

		VkShaderModule frgModule;
		vkCreateShaderModule(g.lDev, &frgi, nullptr, &frgModule);

		VkPipelineShaderStageCreateInfo si[2] = {};
		si[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		si[0].module = vtxModule;
		si[0].pName = "main";

		si[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		si[1].module = frgModule;
		si[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo vi = {};

		VkPipelineInputAssemblyStateCreateInfo ii = {};
		ii.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

		VkPipelineViewportStateCreateInfo vpi = {};
		vpi.viewportCount = 1;
		vpi.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rai = {};
		rai.cullMode = VK_CULL_MODE_BACK_BIT;
		rai.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo mi = {};
		mi.rasterizationSamples = (g.windowFlags & FLAG_MSAA_4X_HINT) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo di = {};
		di.depthTestEnable = true;
		di.depthWriteEnable = true;
		di.depthCompareOp = VK_COMPARE_OP_GREATER;

		VkPipelineColorBlendAttachmentState as = {};
		as.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		as.blendEnable = true;
		as.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		as.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		as.colorBlendOp = VK_BLEND_OP_ADD;
		as.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		as.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		as.alphaBlendOp = VK_BLEND_OP_ADD;


		VkPipelineColorBlendStateCreateInfo bi = {};
		bi.attachmentCount = 1;
		bi.pAttachments = &as;

		VkDynamicState ds[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		VkPipelineDynamicStateCreateInfo dsi = {};
		dsi.dynamicStateCount = 2;
		dsi.pDynamicStates = ds;

		VkGraphicsPipelineCreateInfo ci = {};
		ci.pNext = &ri;
		ci.stageCount = 2;
		ci.pStages = si;
		ci.pVertexInputState = &vi;
		ci.pInputAssemblyState = &ii;
		ci.pViewportState = &vpi;
		ci.pRasterizationState = &rai;
		ci.pMultisampleState = &mi;
		ci.pDepthStencilState = &di;
		ci.pColorBlendState = &bi;
		ci.pDynamicState = &dsi;
		ci.layout = g.layout;

		vkCreateGraphicsPipelines(g.lDev, nullptr, 1, &ci, nullptr, &g.wirePipe);

		vkDestroyShaderModule(g.lDev, vtxModule, nullptr);

		vtxi.codeSize = solid_vert_size * sizeof(uint32_t);
		vtxi.pCode = solid_vert;
		vkCreateShaderModule(g.lDev, &vtxi, nullptr, &vtxModule);
		si[0].module = vtxModule;

		ii.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		vkCreateGraphicsPipelines(g.lDev, nullptr, 1, &ci, nullptr, &g.solidPipe);

		vkDestroyShaderModule(g.lDev, vtxModule, nullptr);
		vkDestroyShaderModule(g.lDev, frgModule, nullptr);
	}

	// Buffers
	{
		for(int i = 0; i < gFramesInFlight; i++) {
			g.staging[i] = createBuffer(sizeof(Cube) * (50 * 50 * 50 * 2 + 1), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		g.cubes = createBuffer(sizeof(Cube) * (50 * 50 * 50 * 2 + 1), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}
}

bool WindowShouldClose(void) {
	return glfwWindowShouldClose(g.win);
}

void CloseWindow(void) {
	vkDeviceWaitIdle(g.lDev);

	destroyBuffer(g.cubes);
	for(int i = 0; i < gFramesInFlight; i++) {
		destroyBuffer(g.staging[i]);
	}

	for(VkImageView view : g.views) {
		vkDestroyImageView(g.lDev, view, nullptr);
	}
	g.views.resize(0);

	destroyImage(g.msaa);
	destroyImage(g.ds);

	for(int i = 0; i < gFramesInFlight; i++) {
		vkDestroyCommandPool(g.lDev, g.perFrame[i].cmdPool, nullptr);
		vkDestroySemaphore(g.lDev, g.perFrame[i].acquireSem, nullptr);
		vkDestroySemaphore(g.lDev, g.perFrame[i].presentSem, nullptr);
		vkDestroyFence(g.lDev, g.perFrame[i].fence, nullptr);
	}

	vkDestroyPipeline(g.lDev, g.solidPipe, nullptr);
	vkDestroyPipeline(g.lDev, g.wirePipe, nullptr);
	vkDestroyPipelineLayout(g.lDev, g.layout, nullptr);

	vkDestroySwapchainKHR(g.lDev, g.swap, nullptr);
	vkDestroyDevice(g.lDev, nullptr);

	vkDestroySurfaceKHR(g.inst, g.surf, nullptr);
	vkDestroyInstance(g.inst, nullptr);

	glfwDestroyWindow(g.win);
	glfwTerminate();
}

bool IsKeyPressed(int key) {
	return !g.prevKeys[key] && g.keys[key];
}

bool IsKeyDown(int key) {
	return g.keys[key];
}

bool IsMouseButtonPressed(int button) {
	return !g.prevKeys[button] && g.keys[button];
}

Vector2 GetMouseDelta(void) {
	return { g.mousePos - g.prevMousePos };
}

float GetMouseWheelMove(void) {
	return g.scroll;
}

Matrix GetCameraMatrix(Camera camera) {
	return glm::lookAt(camera.position, camera.target, camera.up);
}

void ClearBackground(Color color) {
	glm::vec4 fColor = glm::vec4(color) / glm::vec4(255.0f);
	g.col = { fColor.r, fColor.g, fColor.b, fColor.a };
}

void BeginDrawing(void) {

}

void EndDrawing(void) {
	vkWaitForFences(g.lDev, 1, &g.perFrame[g.idx % gFramesInFlight].fence, true, std::numeric_limits<uint64_t>::max());

	VkResult result = VK_ERROR_OUT_OF_DATE_KHR;
	while(result == VK_ERROR_OUT_OF_DATE_KHR) {
		result = vkAcquireNextImageKHR(g.lDev, g.swap, std::numeric_limits<uint64_t>::max(), g.perFrame[g.idx % gFramesInFlight].acquireSem, nullptr, &g.img);
		if(result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapchain();
		}
	}

	vkResetFences(g.lDev, 1, &g.perFrame[g.idx % gFramesInFlight].fence);
	vkResetCommandPool(g.lDev, g.perFrame[g.idx % gFramesInFlight].cmdPool, 0);

	VkCommandBufferBeginInfo bi = {};
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, &bi);

	VkMemoryBarrier2 mb = {};
	mb.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
	mb.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
	mb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	mb.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;

	VkDependencyInfo di = {};
	di.memoryBarrierCount = 1;
	di.pMemoryBarriers = &mb;

	vkCmdPipelineBarrier2(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, &di);

	memcpy(reinterpret_cast<char*>(g.staging[g.idx % gFramesInFlight].hostPtr), g.wires.data(), g.wires.size() * sizeof(Cube));
	memcpy(reinterpret_cast<char*>(g.staging[g.idx % gFramesInFlight].hostPtr) + g.wires.size() * sizeof(Cube), g.solids.data(), g.solids.size() * sizeof(Cube));

	VkBufferCopy bc = {};
	bc.size = (g.wires.size() + g.solids.size()) * sizeof(Cube);

	vkCmdCopyBuffer(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, g.staging[g.idx % gFramesInFlight].buffer, g.cubes.buffer, 1, &bc);

	std::swap(mb.srcStageMask, mb.dstStageMask);
	std::swap(mb.srcAccessMask, mb.dstAccessMask);

	bool msaa = g.windowFlags & FLAG_MSAA_4X_HINT;
	VkImageSubresourceRange colorRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageSubresourceRange depthRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

	std::vector<VkImageMemoryBarrier2> barriers(msaa ? 3 : 2);
	barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	barriers[0].dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barriers[0].image = g.images[g.img];
	barriers[0].subresourceRange = colorRange;

	barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	barriers[1].srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
	barriers[1].dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	barriers[1].image = g.ds.image;
	barriers[1].subresourceRange = depthRange;

	if(msaa) {
		barriers[2].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barriers[2].srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		barriers[2].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barriers[2].dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		barriers[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[2].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barriers[2].image = g.msaa.image;
		barriers[2].subresourceRange = colorRange;
	}

	di.imageMemoryBarrierCount = barriers.size();
	di.pImageMemoryBarriers = barriers.data();

	vkCmdPipelineBarrier2(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, &di);

	VkViewport vp = { 0.0f, 0.0f, static_cast<float>(g.width), static_cast<float>(g.height), 0.0f, 1.0f };
	VkRect2D sc = { { 0, 0 }, { static_cast<uint32_t>(g.width), static_cast<uint32_t>(g.height) } };
	vkCmdSetViewport(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, 0, 1, &vp);
	vkCmdSetScissor(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, 0, 1, &sc);

	VkRenderingAttachmentInfo ai1 = {};
	ai1.imageView = msaa ? g.msaa.view : g.views[g.img];
	ai1.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	ai1.resolveMode = msaa ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE;
	ai1.resolveImageView = g.views[g.img];
	ai1.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	ai1.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ai1.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
	ai1.clearValue = { g.col };

	VkRenderingAttachmentInfo ai2 = {};
	ai2.imageView = g.ds.view;
	ai2.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	ai2.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ai2.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	ai2.clearValue = { 0.0f };

	VkRenderingInfo ri = {};
	ri.renderArea = { 0, 0, { static_cast<uint32_t>(g.width), static_cast<uint32_t>(g.height) } };
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &ai1;
	ri.pDepthAttachment = &ai2;

	vkCmdBeginRendering(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, &ri);

	PushConstants pcs;
	pcs.buf = g.cubes.devicePtr;
	pcs.offs = g.wires.size();
	pcs.trans = g.transform;
	vkCmdPushConstants(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, g.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pcs);
	vkCmdBindPipeline(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g.wirePipe);
	vkCmdDraw(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, g.wires.size() * 24, 1, 0, 0);

	vkCmdBindPipeline(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g.solidPipe);
	vkCmdDraw(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, g.solids.size() * 36, 1, 0, 0);

	vkCmdEndRendering(g.perFrame[g.idx % gFramesInFlight].cmdBuffer);

	VkImageMemoryBarrier2 ib = {};
	ib.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	ib.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	ib.image = g.images[g.img];
	ib.subresourceRange = colorRange;

	di = {};
	di.imageMemoryBarrierCount = 1;
	di.pImageMemoryBarriers = &ib;

	vkCmdPipelineBarrier2(g.perFrame[g.idx % gFramesInFlight].cmdBuffer, &di);

	vkEndCommandBuffer(g.perFrame[g.idx % gFramesInFlight].cmdBuffer);

	VkSemaphoreSubmitInfo ssi1 = {};
	ssi1.semaphore = g.perFrame[g.idx % gFramesInFlight].acquireSem;
	ssi1.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSemaphoreSubmitInfo ssi2 = {};
	ssi2.semaphore = g.perFrame[g.idx % gFramesInFlight].presentSem;
	ssi2.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkCommandBufferSubmitInfo csi = {};
	csi.commandBuffer = g.perFrame[g.idx % gFramesInFlight].cmdBuffer;

	VkSubmitInfo2 si = {};
	si.waitSemaphoreInfoCount = 1;
	si.pWaitSemaphoreInfos = &ssi1;
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &csi;
	si.signalSemaphoreInfoCount = 1;
	si.pSignalSemaphoreInfos = &ssi2;

	vkQueueSubmit2(g.q, 1, &si, g.perFrame[g.idx % gFramesInFlight].fence);

	VkPresentInfoKHR pi = {};
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &g.perFrame[g.idx % gFramesInFlight].presentSem;
	pi.swapchainCount = 1;
	pi.pSwapchains = &g.swap;
	pi.pImageIndices = &g.img;

	if(vkQueuePresentKHR(g.q, &pi) != VK_SUCCESS) {
		recreateSwapchain();
	}

	g.solids.clear();
	g.wires.clear();

	g.idx++;

	double time = glfwGetTime();
	while(time - g.curTime < g.frameTime) {
		time = glfwGetTime();
	}
	
	g.curTime = time;

	g.scroll = 0.0;
	g.prevMousePos = g.mousePos;
	memcpy(&g.prevKeys, &g.keys, sizeof(g.keys));

	glfwPollEvents();
}

void BeginMode3D(Camera3D camera) {
	g.transform = perspective(glm::radians(camera.fovy), static_cast<float>(g.width) / g.height, 0.01f) * glm::lookAt(camera.position, camera.target, camera.up);
}

void EndMode3D(void) {

}

void rlEnableBackfaceCulling(void) {

}

void DrawCube(Vector3 position, float width, float height, float length, Color color) {
	g.solids.push_back(Cube{ position, glm::vec3(width, height, length), color });
}

void DrawCubeWires(Vector3 position, float width, float height, float length, Color color) {
	g.wires.push_back(Cube{ position, glm::vec3(width, height, length), color });
}

void DrawFPS(int posX, int posY) {

}

float Vector3DotProduct(Vector3 v1, Vector3 v2) {
	return glm::dot(v1, v2);
}

