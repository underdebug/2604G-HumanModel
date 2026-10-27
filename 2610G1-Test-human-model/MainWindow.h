#pragma once
#include "VulkanWindow.h"
#include <QPointer>
#include <functional>
#include <vector>
#include "makehuman.h"
#include "ClothSimulation.h"
#include <QElapsedTimer>
class MainWindow : public VulkanWindow
{
public:
    explicit MainWindow(QWindow *parent = nullptr);
    ~MainWindow() override;
    void openObjFile(const std::string &path);
    void showCubeMesh();
    void setClothState(bool enabled, bool running, float wind);
    void resetCloth();
    void setShirtEnabled(bool enabled);
    void applyBodyControls(QObject *controls);
    void setNavigationControls(QObject *controls);
    void applyCameraControls();
    void navigationChanged() override;
    std::function<void()> prepareNavigation;
protected:
    bool event(QEvent *event) override;
    void initializeScene(VkPhysicalDevice physical, VkDevice device) override;
    void releaseScene() override;
    void calculateScene(VkCommandBuffer, VkQueue, VkFence) override;
    void createScenePipeline(VkRenderPass renderPass) override;
    void releaseScenePipeline() override;
    void recordScene(VkCommandBuffer command, const std::array<float, 16> &matrix, VkExtent2D extent) override;
private:
    std::vector<makehuman::MeshVertex> loadObjTriangles(const std::string &path) const;
    std::vector<makehuman::MeshVertex> buildCubeVertices() const;
    QPointer<QObject> navigationControls;
    std::vector<makehuman::MeshVertex> vertices;
    bool meshDirty = true;
    bool humanModel = false, clothEnabled = false, clothRunning = true;
    float clothWind = 0.8f;
    bool shirtEnabled = true;
    std::vector<makehuman::MeshVertex> shirt;
    ClothSimulation cloth;
    QElapsedTimer clothClock;
    VkDeviceSize bufferCapacity = 0;
    uint32_t drawCount = 0;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer inputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory inputMemory = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
};
