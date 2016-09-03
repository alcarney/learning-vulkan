#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

/*
 * This struct will tell us which index? a certain
 * queue family can be found. -1 denotes the family
 * not being found.
 */
struct QueueFamilyIndices {
    int graphicsFamily = -1;

    bool isComplete() {
        return graphicsFamily >= 0;
    }
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

        // Reference to the hardware we will run on
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

        // Reference to the logical device we will use
        VDeleter<VkDevice> device{vkDestroyDevice};

        // Reference to our graphics queue
        VkQueue graphicsQueue;

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

            // Step 3: Choosing a hardware device
            pickPhysicalDevice();

            // Step 4: Creating a logical device
            createLogicalDevice();
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

            // For our purposes we need a queue which supports
            // VK_QUEUE_GRAPHICS_BIT
            int i = 0;

            for (const auto& queueFamily : queueFamilies) {
                if (queueFamily.queueCount > 0 &&
                        queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    indices.graphicsFamily = i;
                }

                if (indices.isComplete()) {
                    break;
                }

                i++;
            }

            return indices;
        }

        /*
         * This function will look at a physical device and decide if it is
         * "suitable"
         *
         * In our case a device is suitable if it supports a graphics queue
         */
        bool isDeviceSuitable(VkPhysicalDevice device) {

            QueueFamilyIndices indices = findQueueFamilies(device);
            return indices.isComplete();
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

            VkDeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = indices.graphicsFamily;
            queueCreateInfo.queueCount = 1;

            // Even though we have a single queue the scheduler will want us
            // to assign a priority to it
            float queuePriority = 1.0f;
            queueCreateInfo.pQueuePriorities = &queuePriority;

            // Eventually we will have to specify the features of the device
            // we will need
            VkPhysicalDeviceFeatures deviceFeatures = {};

            // Finally we can bring this together and specify the features of
            // the logical device we need, starting with the desired queues and
            // device features.
            VkDeviceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pQueueCreateInfos = &queueCreateInfo;
            createInfo.queueCreateInfoCount = 1;
            createInfo.pEnabledFeatures = &deviceFeatures;

            // As with the instance we need to specify any validation
            // layers we want applied to the device
            createInfo.enabledExtensionCount = 0;

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
