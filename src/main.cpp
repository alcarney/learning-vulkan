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
            this->deleter = [=](T obj) {delete(obj, nullptr); };
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

        void initVulkan() {}
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
