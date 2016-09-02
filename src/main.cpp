#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

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
