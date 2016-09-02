#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <functional>

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

        // The GLFW window object
        GLFWwindow* window;

        // The Vulkan instnce object
        VDeleter<VkInstance> instance {vkDestroyInstance};

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
        }

        /*
         * This function is responsible for creating the vulkan instance
         */
        void createInstance() {

            // First off we need to tell the driver some information about our
            // application
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

            unsigned int glfwExtensionCount = 0;
            const char** glfwExtensions;

            // Ask GLFW for the extensions it needs to get vulkan ta;lking to
            // the windowing system
            glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

            // And tell Vulkan about them
            createInfo.enabledExtensionCount = glfwExtensionCount;
            createInfo.ppEnabledExtensionNames = glfwExtensions;

            // Now tell Vulkan about the validation layers we will be using
            // (None for now)
            createInfo.enabledLayerCount = 0;

            // Finally we have everything in place, time to tell Vulkan to make
            // an instance for us
            if(vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {

                throw std::runtime_error("Failed to create instance!!");
            }
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
