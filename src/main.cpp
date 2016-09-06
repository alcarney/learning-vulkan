#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

/*
 * This struct will tell us which index? a certain
 * queue family can be found. -1 denotes the family
 * not being found.
 */
struct QueueFamilyIndices {
    int graphicsFamily = -1;
    int presentFamily = -1;

    bool isComplete() {
        return graphicsFamily >= 0 &&
               presentFamily >= 0;
    }
};

/*
 * This struct will hold the capabilities of a particular
 * device's swap chain support. We will need this to make sure
 * the device we choose is compatible with our paricular window
 * surface
 */
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

/*
 * This function looks up the debug callback function
 * destructor and loads it for us
 */
void DestroyDebugReportCallbackEXT(VkInstance instance,
                                   VkDebugReportCallbackEXT callback,
                                   const VkAllocationCallbacks* pAllocator) {

    // Lookup the function
    auto func = (PFN_vkDestroyDebugReportCallbackEXT)
                  vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");

    // Did it work?
    if (func != nullptr) {
        func(instance, callback, pAllocator);
    }
}

/*
 * This function looks up the debug callback creator function
 * and loads it for us
 */
VkResult CreateDebugReportCallbackEXT(VkInstance instance,
                                      const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDebugReportCallbackEXT* pCallback) {

    // Lookup the address of the function we want
    auto func = (PFN_vkCreateDebugReportCallbackEXT)
                  vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");

    // Did it work?
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pCallback);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

/*
 * This class template will automatically manage our
 * vulkan objects for us, cleaning up when they are
 * no longer needed.
 *
 * However this just seems to be a bunch of dark magic.
 */
template <typename T>
class VDeleter {
    public:

        // Wut..
        VDeleter() : VDeleter([](T, VkAllocationCallbacks*) {}) {}

        // Wut...
        VDeleter(std::function<void(T, VkAllocationCallbacks*)> deletef) {
            this->deleter = [=](T obj) {deletef(obj, nullptr); };
        }

        // Wut..
        VDeleter(const VDeleter<VkInstance>& instance,
                 std::function<void(VkInstance, T, VkAllocationCallbacks*)> deletef) {
            this->deleter = [&instance, deletef](T obj) {deletef(instance, obj, nullptr); };
        }

        // Wut..
        VDeleter(const VDeleter<VkDevice>& device,
                 std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef) {
            this->deleter = [&device, deletef](T obj) {deletef(device, obj, nullptr); };
        }

        ~VDeleter() {
            cleanup();
        }

        T* operator &() {
            cleanup();
            return &object;
        }

        operator T() const {
            return object;
        }

    private:
        T object{VK_NULL_HANDLE};
        std::function<void(T)> deleter;

        void cleanup() {
            if (object != VK_NULL_HANDLE) {
                deleter(object);
            }
            object = VK_NULL_HANDLE;
        }
};

/*
 * Our main class <shudder>...
 *
 * Here is where everything happens we create and manage our vulkan
 * instance and will eventually end up with a triangle on screen.
 */
class App {

    public:
        void run () {
            initWindow();
            initVulkan();
            mainLoop();
        }

    private:

        // Some constants
        const int WIDTH = 800;
        const int HEIGHT = 600;

        // Validation Layers
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_LUNARG_standard_validation"
        };

        // Extensions
        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Do we enable these layers?
        #ifdef NDEBUG
            const bool enableValidationLayers = false;
        #else
            const bool enableValidationLayers = true;
        #endif

        // The GLFW window object
        GLFWwindow* window;

        // The Vulkan instnce object
        VDeleter<VkInstance> instance {vkDestroyInstance};

        // Debug Callback Function
        VDeleter<VkDebugReportCallbackEXT>
            callback {instance, DestroyDebugReportCallbackEXT};

        // Window surface
        VDeleter<VkSurfaceKHR> surface{instance, vkDestroySurfaceKHR};

        // Reference to the hardware we will run on
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

        // Reference to the logical device we will use
        VDeleter<VkDevice> device{vkDestroyDevice};

        // References to our queues
        VkQueue graphicsQueue;
        VkQueue presentQueue;

        // Refernece to the swap chain
        VDeleter<VkSwapchainKHR> swapChain{device, vkDestroySwapchainKHR};

        // Reference to the image queue in the swap chain along the image
        // properties
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;

        // The views into our images
        std::vector<VDeleter<VkImageView>> swapChainImageViews;

        // Pipeline layout
        VDeleter<VkPipelineLayout> pipelineLayout{device, vkDestroyPipelineLayout};

        /*
         * This function invokes GLFW and will create a window for us to
         * display our stuff in.
         */
        void initWindow() {

            // Initialize GLFW
            glfwInit();

            // Tell GLFW that we don't need an OpenGL context..
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

            // Prevent the window from being resized for now
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

            // Create the window
            window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
        }

        // ------------------------ INITIALIZING VULKAN -----------------------

        /*
         * This function performs all the actions necessary to get vulkan to
         * a state to draw something on screen.
         */
        void initVulkan() {

            // Step 1: Create an instance
            createInstance();

            // Step 2: Setup debug callbacks
            setupDebugCallback();

            // Step 3: Creating a surface
            createSurface();

            // Step 4: Choosing a hardware device
            pickPhysicalDevice();

            // Step 5: Creating a logical device
            createLogicalDevice();

            // Step 6: Create the Swap Chain (render queue)
            createSwapChain();

            // Step 7: Create Views into our images
            createImageViews();

            // Step 8: Build the graphics pipeline
            createGraphicsPipeline();
        }

        /*
         * This function is responsible for creating the vulkan instance
         */
        void createInstance() {

            // First are all of our validation layers available? - if needed
            if (enableValidationLayers && !checkValidationLayerSupport()) {
                throw std::runtime_error("Validation layers requested, but not available!!");
            }

            // Tell the driver some information about our  application
            VkApplicationInfo appInfo = {};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "Demo Triangle";
            appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.pEngineName = "No Name";
            appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
            appInfo.apiVersion = VK_API_VERSION_1_0;

            // Next is another struct which tells Vulkan which extensions
            // and validation layers we will want to use
            VkInstanceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;

            // Add the extensions we will need
            auto extensions = getRequiredExtensions();

            // And tell Vulkan about them
            createInfo.enabledExtensionCount = extensions.size();
            createInfo.ppEnabledExtensionNames = extensions.data();

            // Only enable validation layers if needed
            if (enableValidationLayers) {
                createInfo.enabledLayerCount = validationLayers.size();
                createInfo.ppEnabledLayerNames = validationLayers.data();

            } else {
                createInfo.enabledLayerCount = 0;
            }


            // Finally we have everything in place, time to tell Vulkan to make
            // an instance for us
            if(vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {

                throw std::runtime_error("Failed to create instance!!");
            }
        }

        /*
         * This function checks if particular validation layers are supported
         */
        bool checkValidationLayerSupport() {

            // First count the number of layers
            uint32_t layerCount;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

            // Next allocate enough space for them and get a 'list'
            std::vector<VkLayerProperties> availableLayers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            // Now to check to see if all of our requested layers are available
            for (const char* layerName : validationLayers) {

                // Assume the layer doesn't exist
                bool layerFound = false;

                // Go through each available layer and try to find it
                for(const auto& layerProperties : availableLayers) {
                    if (strcmp(layerName, layerProperties.layerName) == 0) {
                        layerFound = true;
                        break;
                    }
                }

                // If we didn#t find it then not all of our layers are supported
                if(!layerFound) {
                    return false;
                }
            }

            // If we get this far then all layers are supported :D
            return true;

        }

        /*
         * Callback function, this will let Vulkan report what our
         * validation layers are saying
         */
        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags,
                                                            VkDebugReportObjectTypeEXT objType,
                                                            uint64_t obj,
                                                            size_t location,
                                                            int32_t code,
                                                            const char* layerPrefix,
                                                            const char* msg,
                                                            void* userData) {
            std::cerr << "Validation layer: " << msg << std::endl;

            return VK_FALSE;
        }

        void setupDebugCallback() {
            if(!enableValidationLayers) return;

            // We need to tell Vulkan about our function
            VkDebugReportCallbackCreateInfoEXT createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;

            // Which events do we want to handle?
            createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
            createInfo.pfnCallback = debugCallback;

            // Try and setup the callback
            if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback)
                    != VK_SUCCESS) {
                throw std::runtime_error("Failed to setup the debug callback!!");
            }
        }

        /*
         * This function will return a list of all the extensions we require
         */
        std::vector<const char*> getRequiredExtensions() {

            // Create a list for all of the extension strings
            std::vector<const char*> extensions;

            unsigned int glfwExtensionCount = 0;
            const char** glfwExtensions;

            // Ask GLFW for the extensions it needs to get vulkan ta;lking to
            // the windowing system
            glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

            // Add the GLFW extensions to the list
            for (unsigned int i = 0; i < glfwExtensionCount; i++) {
                extensions.push_back(glfwExtensions[i]);
            }

            // Finally if we are using validation layers, we also need to add the
            // debug extension
            if (enableValidationLayers) {
                extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            }

            return extensions;
        }

        /*
         * This function will create the surface that will allow us to draw
         * stuff
         */
        void createSurface() {
            if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
                throw std::runtime_error("Unable to create the window surafce!!");
            }
        }

        /*
         * This function is responsible for choosing the hardware device to run on
         */
        void pickPhysicalDevice() {

            // We will choose the device from a list of available hardware
            // but first off we need to count them all.
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

            // If no devices are found then we won't be able to do anything!!
            if (deviceCount == 0) {
                throw std::runtime_error("Unable to find Vulkan compatible hardware!!");
            }

            // Put all the available devices in a list
            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

            // We will pick the first device that matches our needs
            for (const auto& device : devices) {
                if (isDeviceSuitable(device)) {
                    physicalDevice = device;
                    break;
                }
            }

            // If nothing is suitable then...
            if (physicalDevice == VK_NULL_HANDLE) {
                throw std::runtime_error("Unable to find a suitable device!!");
            }

        }

        /*
         * This will return the indices of the queue families
         */
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
            QueueFamilyIndices indices;

            // Get all of the queue type supported on this device
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

            int i = 0;

            for (const auto& queueFamily : queueFamilies) {

                // Check for graphics queue support
                if (queueFamily.queueCount > 0 &&
                        queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    indices.graphicsFamily = i;
                }

                // Check for 'present support'
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

                if (queueFamily.queueCount > 0 && presentSupport) {
                    indices.presentFamily = i;
                }

                if (indices.isComplete()) {
                    break;
                }

                i++;
            }

            return indices;
        }

        /*
         * This will get the details of the particular swap chain we can create
         */
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {

            SwapChainSupportDetails details;

            // First query the capabilities
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

            // Next we will count the number of formats
            uint32_t formatCount;
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

            // Then actually pull them out
            if (formatCount != 0) {
                details.formats.resize(formatCount);
                vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
            }

            // Similarly for the present modes
            uint32_t presentModeCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

            if(presentModeCount != 0) {
                details.presentModes.resize(presentModeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
            }

            return details;

        }

        /*
         * This function will choose the format we will draw to the surface with
         */
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {

            // Best Case: The surface doesn't care what format we use, in which
            // case we simply choose our preference
            if (availableFormats.size() == 1 &&
                    availableFormats[0].format == VK_FORMAT_UNDEFINED) {
                return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
            }

            // If not... then we will have to check the formats to see if our preferred
            // format pairing shows up
            for (const auto& availableFormat : availableFormats) {
                if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
                    availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    return availableFormat;
                }
            }

            /*
             * If we get to here then our ideal pairing isn't supported then we
             * could start trying to come up with a ranking so we choose the
             * "best" one. But effort... so we'll just use the first one we
             * come accross
             */
            return availableFormats[0];
        }

        /*
         * This function chooses the presentation mode to the screen. This
         * setting will directly affect how the application 'feels' According
         * to the tutorial, there are four possible modes:
         *
         *   - VK_PRESENT_MODE_IMMEDIATE_KHR: This corresponds to V-Sync off
         *     in video games. Frames are drawn as they received by the swap
         *     chain. Can result in tearing. If the queue is full the application
         *     must wait for the screen to catch up.
         *
         *   - VK_PRESENT_MODE_FIFO_KHR: This corresponds to V-Sync on in
         *     video games. It takes an image from the front of the queue
         *     at the start of a draw cycle. This is the only guaranteed to be
         *     present
         *
         *   - VK_PRESENT_MODE_FIFO_RELAXED_KHR: Similar to the previous mode
         *     except if the application is slow it will insert the next frame
         *     as it arrives instead of waiting for the next draw cycle. Can
         *     result in tearing.
         *
         *   - VK_PRESENT_MODE_MAILBOX_KHR: A variant on the first mode, except
         *     that if the queue is full then the frames are just overwritten.
         *     Can be used to implement triple buffering.
         */
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {

            // We will aim for triple buffering but if that's not available
            // we will fall back to plain old double buffering
            for (const auto& availablePresentMode : availablePresentModes) {
                if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return availablePresentMode;
                }
            }

            return VK_PRESENT_MODE_FIFO_KHR;
        }

        /*
         * This function will choose the extent of the swap chain. Which is a
         * posh way of saying the resolution of the frames we will render.
         * So ideally we want it equal to the size of the window we render
         * to
         */
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {

            // Some magic here about window managers and special values I
            // don't quite understand yet...
            if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
                return capabilities.currentExtent;
            } else {

                VkExtent2D actualExtent = {WIDTH, HEIGHT};

                actualExtent.width = std::max(capabilities.minImageExtent.width,
                                              std::min(capabilities.maxImageExtent.width,
                                                       actualExtent.width));

                actualExtent.height = std::max(capabilities.minImageExtent.height,
                                               std::min(capabilities.maxImageExtent.height,
                                                        actualExtent.height));

                return actualExtent;
            }
        }

        /*
         * After we choose the format for our surface, the presentation
         * mode and the image resolution we can finally build the swap
         * chain.
         */
        void createSwapChain () {

            // Query the capabilities of the system
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

            // Now pick the surface format...
            VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);

            // ... the presenation mode ...
            VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);

            // ... and the extent (image resolution).
            VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

            // Still a few more decsisions to be made before we can create the chain.
            // First of all is how many images in the queue do we want?
            uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

            // But let's just make sure we don't ask for more images than the implementation
            // can provide for us
            if (swapChainSupport.capabilities.maxImageCount > 0 &&
                imageCount > swapChainSupport.capabilities.maxImageCount) {
                imageCount = swapChainSupport.capabilities.maxImageCount;
            }

            // Now we can start filling in the structure which will describe the
            // swap chain we want created for us. Be Warned though! It's a biggie
            VkSwapchainCreateInfoKHR createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            createInfo.surface = surface;

            // Image details
            createInfo.minImageCount = imageCount;
            createInfo.imageFormat = surfaceFormat.format;
            createInfo.imageColorSpace = surfaceFormat.colorSpace;
            createInfo.imageExtent = extent;
            createInfo.imageArrayLayers = 1;                                // Number of layers per image, always 1 unless going for 3D
            createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

            /*
             * Now it could be the case where our graphicQueue (where we collect images
             * to render to) is different from the presentQueue (where we submit rendered
             * images to be drawn on screen). So we need to check for it and configure
             * the swap chain to cope with this accordingly
             */
            QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
            uint32_t queueFamilyIndices[] = {(uint32_t) indices.graphicsFamily,
                                             (uint32_t) indices.presentFamily};

            if (indices.graphicsFamily != indices.presentFamily) {

                /*
                 * In the case where the queues are different we need to
                 * tell Vulkan that the images will be shared accross queues,
                 * how many queues and which ones.
                 *
                 * We will also use VK_SHARING_MODE_CONCURRENT, which allows us
                 * to share the images without explicitly transferring ownership.
                 * This will hit the performance.
                 */
                createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                createInfo.queueFamilyIndexCount = 2;
                createInfo.pQueueFamilyIndices = queueFamilyIndices;
            } else {

                /*
                 * In this case the queues are the same so we can set the
                 * ownership of the images to be exclusive to the queue.
                 * If we want to use an image elsewhere, we will have to explicitly
                 * handle ownership transfers.
                 *
                 * This option will result in better performance
                 */
                createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            }

            // We can also choose a transformation applied to images in the queue
            createInfo.preTransform = swapChainSupport.capabilities.currentTransform;

            // Opacity of the images
            createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

            // Specify the presentation mode
            createInfo.presentMode = presentMode;
            createInfo.clipped = VK_TRUE;

            /*
             * It's possible for the swap chain to be invalidated during the execution
             * of the program. For example the window is resized invalidatin the
             * extent we have already defined. This means we would have to build a new
             * swap chain and swap out the old one. So we need to give Vulkan a reference
             * to the old one when we create the new one.
             *
             * At this early stage of the tutorial though we will assume that we will
             * never need a new one, so there will never be an old one.
             */
            createInfo.oldSwapchain = VK_NULL_HANDLE;

            // Finally!! Try and create the Swap Chain
            if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
                throw std::runtime_error("Unable to create the swap chain!!");
            }

            /*
             * The vulkan implementation is allowed to create more images than we asked for
             * so we need to check to see how many it actually created for us, along with
             * getting the reference to the image queue created by the swap chain.
             */
            vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
            swapChainImages.resize(imageCount);
            vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

            // Also don't forget to make a note of the extent and image format we chose
            // as we'll need those later.
            swapChainImageFormat = surfaceFormat.format;
            swapChainExtent = extent;

        }

        /*
         * This function is responsible for creating the views into our images
         */
        void createImageViews() {

            // Resize our views to match the number of images in the swap chain
            swapChainImageViews.resize(swapChainImages.size(),
                                       VDeleter<VkImageView>{device, vkDestroyImageView});

            // Next for each image in the chain
            for (uint32_t i = 0; i < swapChainImages.size(); i++) {

                // We will make a view for it
                VkImageViewCreateInfo createInfo = {};
                createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                createInfo.image = swapChainImages[i];

                // What will the image represent
                createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                createInfo.format = swapChainImageFormat;

                // Swizzle the components...?
                createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
                createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
                createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
                createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

                // Some more info on how the image should be used
                createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                createInfo.subresourceRange.baseMipLevel = 0;
                createInfo.subresourceRange.levelCount = 1;
                createInfo.subresourceRange.baseArrayLayer = 0;
                createInfo.subresourceRange.layerCount = 1;

                // Create the image view
                if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i])
                        != VK_SUCCESS) {
                    throw std::runtime_error("Unable to create image views!!");
                }
            }
        }

        /*
         * This function checks to see of the Vulkan extensions we require are
         * supported
         */
        bool checkDeviceExtensionSupport(VkPhysicalDevice device) {

            // So first we count the number of available extensions
            uint32_t extensionCount;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

            // Grab all of the available extensions
            std::vector<VkExtensionProperties> availableExtensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

            // We create a set of all the extensions we require
            std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

            // And remove the ones we find in the available extensions
            for (const auto& extension : availableExtensions) {
                requiredExtensions.erase(extension.extensionName);
            }

            // If all extensions are supported then the set will be empty
            return requiredExtensions.empty();
        }

        /*
         * This function will look at a physical device and decide if it is
         * "suitable"
         *
         * In our case a device is suitable if:
         *   - it supports a graphics queue
         *   - it has present support
         *   - there is a present mode and image format available which is
         *     compitable with our window's surface.
         */
        bool isDeviceSuitable(VkPhysicalDevice device) {

            QueueFamilyIndices indices = findQueueFamilies(device);

            bool extensionsSupported = checkDeviceExtensionSupport(device);

            bool swapChainAdequate = false;

            if (extensionsSupported) {
                SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
                swapChainAdequate = !swapChainSupport.formats.empty() &&
                                    !swapChainSupport.presentModes.empty();
            }

            return indices.isComplete() &&
                   extensionsSupported  &&
                   swapChainAdequate;
        }

        /*
         * This function will setup the logical device.
         *
         * The logical device is responsible for controlling our
         * hardware device??
         */
        void createLogicalDevice() {

            // First we need to specify the queues we want created
            // For now a single graphics queue will suffice
            QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

            std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
            std::set<int> uniqueQueueFamilies =
                {indices.graphicsFamily, indices.presentFamily};

            float queuePriority = 1.0f;
            for (int queueFamily : uniqueQueueFamilies) {

                VkDeviceQueueCreateInfo queueCreateInfo = {};
                queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = queueFamily;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &queuePriority;

                queueCreateInfos.push_back(queueCreateInfo);
            }

            // Eventually we will have to specify the features of the device
            // we will need
            VkPhysicalDeviceFeatures deviceFeatures = {};

            // Finally we can bring this together and specify the features of
            // the logical device we need, starting with the desired queues and
            // device features.
            VkDeviceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pQueueCreateInfos = queueCreateInfos.data();
            createInfo.queueCreateInfoCount = (uint32_t) queueCreateInfos.size();
            createInfo.pEnabledFeatures = &deviceFeatures;

            // As with the instance we need to specify any validation
            // layers or extensions we want applied to the device
            createInfo.enabledExtensionCount = deviceExtensions.size();
            createInfo.ppEnabledExtensionNames = deviceExtensions.data();

            if (enableValidationLayers) {
                createInfo.enabledLayerCount = validationLayers.size();
                createInfo.ppEnabledLayerNames = validationLayers.data();
            } else {
                createInfo.enabledLayerCount = 0;
            }

            // So we can now actually create the device
            if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device)
                    != VK_SUCCESS) {
                throw std::runtime_error("Unable to create the logical device!!");
            }

            // With our logical device created the queues we asked for will also
            // have been created, Time to find out where they live...
            vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
            vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
        }

        /*
         * This function will go and fetch the shader data
         * from file for us
         */
        static std::vector<char> readFile(const std::string& filename) {

            // Binary file. start reading from the end
            std::ifstream file (filename, std::ios::ate | std::ios::binary);

            if (!file.is_open()) {
                throw ("Unable to open file!!");
            }

            // By reading the file from the end we have an easy way to determine
            // the size of the file
            size_t fileSize = (size_t) file.tellg();
            std::vector<char> buffer(fileSize);

            // Now we can just jump to the start and read everything in
            file.seekg(0);
            file.read(buffer.data(), fileSize);

            file.close();
            return buffer;
        }

        /*
         * This function sets about making the shader modules.
         */
        void createShaderModule(const std::vector<char>& code, VDeleter<VkShaderModule>& shaderModule) {

            VkShaderModuleCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = code.size();
            createInfo.pCode = (uint32_t*) code.data();

            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule)
                    != VK_SUCCESS) {
                throw std::runtime_error("Unable to create shader module!!");
            }
        }

        /*
         * This function will set up the graphics pipeline for us
         */
        void createGraphicsPipeline () {

            // Read in the shader code
            auto vertShaderCode = readFile("vert.spv");
            auto fragShaderCode = readFile("frag.spv");

            // Now we need to wrap the code in a shader module
            VDeleter<VkShaderModule> vertShaderModule{device, vkDestroyShaderModule};
            VDeleter<VkShaderModule> fragShaderModule{device, vkDestroyShaderModule};

            createShaderModule(vertShaderCode, vertShaderModule);
            createShaderModule(fragShaderCode, fragShaderModule);

            // Then we need to assemble the modules into stages
            // telling Vulkand their purpose
            VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
            vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = vertShaderModule;
            vertShaderStageInfo.pName = "main";  // Function in the shader to invoke

            VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
            fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName = "main";  // Function in the shader to invoke

            VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

            // With the shaders created we need to tell Vulkan the format of
            // our vertex data that we will be passing to it.
            //
            // Since we are hard coding values for now, we will simply tell
            // Vulkan we aren't passing any data
            VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;

            /*
             * After specifying the data format, we tell Vulkan how it would be
             * used, possible values include:
             *
             *   - POINT_LIST: Each vertex will form an individual point
             *   - LINE_LIST : Each pair of vertices will form a line
             *   - LINE_STRIP: All vertices will join to create a joined line
             *   - TRIANGLE_LIST: Each triplet of vertices will form disticnt triangles
             *   - TRIANGLE_STRIP: All vertices will join to form a strip of triangles.
             */
            VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            /*
             * With the pipeline input taken care of, it's time to specify the viewport
             * which is the region of the frambuffer we will render to.
             */
            VkViewport viewport = {};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float) swapChainExtent.width;
            viewport.height = (float) swapChainExtent.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            // And we can define 'scissors' which can be used to restrict
            // regions of the viewport
            VkRect2D scissor = {};
            scissor.offset = {0, 0};
            scissor.extent = swapChainExtent;

            // Combining these two will give us the Viewport State
            VkPipelineViewportStateCreateInfo viewportState = {};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            // Next we configure the rasterizer
            VkPipelineRasterizationStateCreateInfo rasterizer = {};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;       // VK_TRUE, requires a feature.
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;  // Other values require a feature.
            rasterizer.lineWidth = 1.0f;        // Higher values require wideLines feature.
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; // Faces are visible from one side only
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            /*
             * Multi Sampling - Requires a Feature!
             *
             * This can be used to do anti-aliasing
             */
            VkPipelineMultisampleStateCreateInfo multisampling = {};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            /*
             * Color blending, here we will use Alpha blending
             *
             * You can define different color blends for different
             * framebuffers the following struct configures the blending
             * for a single buffer
             */
            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                                | VK_COLOR_COMPONENT_G_BIT
                                                | VK_COLOR_COMPONENT_B_BIT
                                                | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_TRUE;

            // color = (X) * srcColor (OP) (Y) * dstColor
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;   // (X)
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;  // (Y)
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // (OP)
            // => color = (srcAlpha) * srcColor + (1 - srcAlpha) * dstColor

            // alpha = (A) * srcAlpha (OP) (B) * dstAlpha
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            // => alpha = (1) * srcAlpha + (0) * dstAlpha
            //          = srcAlpha

            /*
             * This will reference all such above structs (we are only using
             * one here) and allows us to define our own coefficients for the
             * calculations above.
             */
            VkPipelineColorBlendStateCreateInfo colorBlending = {};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            /*
             * Dynamic State.
             *
             * There is a small number of options which CAN be defined at run time
             * however we won't be doing this for now
             */

            /*
             * There are things called UNIFORM values that we can use in our shaders
             * these are global values we can set at runtime, to change the behavior
             * of our shaders without rebuilding the entrie pipeline.
             *
             * Even though we aren't using them just yet we still need to create a
             * Pipeline Layout object, which will hold these values.
             */
            VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

            if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout)
                    != VK_SUCCESS) {
                throw std::runtime_error("Unable to create the pipeline layout!!");
            }


        }

        // -----------------------------------------------------------------------

        void mainLoop() {

            // Keep the main window open till it's asked to close
            while (!glfwWindowShouldClose(window)) {
                glfwPollEvents();
            }
        }
};

int main() {
    App app;

    try {
        app.run();
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
