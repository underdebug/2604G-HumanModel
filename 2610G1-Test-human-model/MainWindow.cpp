#include "MainWindow.h"
#include "Clothing.h"
#include <QEvent>
#include <QTimer>
#include <QVariant>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/common.hpp>
#include <stdexcept>
using Matrix4 = std::array<float, 16>;
static void check(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: VkResult " + std::to_string(result));
}

[[noreturn]] static void fail(const char *message)
{
    throw std::runtime_error(message);
}

static uint32_t memoryType(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    fail("No suitable Vulkan memory type");
}

static void createStorageBuffer(VkPhysicalDevice physical, VkDevice device, VkDeviceSize size, VkBuffer &target, VkDeviceMemory &allocation,
                         VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
{
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    check(vkCreateBuffer(device, &info, nullptr, &target), "Create storage buffer");

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, target, &requirements);
    VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    memoryInfo.allocationSize = requirements.size;
    memoryInfo.memoryTypeIndex = memoryType(physical, requirements.memoryTypeBits, flags);
    check(vkAllocateMemory(device, &memoryInfo, nullptr, &allocation), "Allocate storage buffer");
    check(vkBindBufferMemory(device, target, allocation, 0), "Bind storage buffer");
}

static VkShaderModule shader(VkDevice device, const char *name)
{
    const auto directory = std::filesystem::read_symlink("/proc/self/exe").parent_path();
    const auto path = directory / "shader" / name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open shader: " + path.string());

    const auto size = file.tellg();
    if (size <= 0 || size % sizeof(uint32_t) != 0)
        throw std::runtime_error("Invalid SPIR-V size: " + path.string());

    std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Cannot read shader: " + path.string());
    if (words.front() != 0x07230203u)
        throw std::runtime_error("Invalid SPIR-V header: " + path.string());

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * sizeof(uint32_t);
    ci.pCode = words.data();
    VkShaderModule module;
    check(vkCreateShaderModule(device, &ci, nullptr, &module), "Create shader");
    return module;
}

static VkPipeline meshPipeline(VkDevice device, VkRenderPass renderPass,
                               VkPipelineLayout pipelineLayout, const char *vertexShader, bool depthWrite)
{
    VkShaderModule vert = shader(device, vertexShader), frag = shader(device, "mesh.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;

    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkVertexInputBindingDescription binding{0, sizeof(makehuman::MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(makehuman::MeshVertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(makehuman::MeshVertex, color)}};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1;
    rs.cullMode = VK_CULL_MODE_NONE;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    // 组合各阶段配置；管线与当前 renderPass 的附件格式匹配。
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dynamic;
    ci.layout = pipelineLayout;
    ci.renderPass = renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    check(result, "Create mesh pipeline");
    return pipeline;
}

void MainWindow::setNavigationControls(QObject *controls)
{
    navigationControls = controls;
}

void MainWindow::applyCameraControls()
{
    auto *root = navigationControls.data();
    if (!root)
        return;
    spin = root->property("cameraSpin").toFloat();
    tilt = root->property("cameraTilt").toFloat();
    distance = root->property("cameraDistance").toFloat();
    panX = root->property("cameraPanX").toFloat();
    panY = root->property("cameraPanY").toFloat();
    panZ = root->property("cameraPanZ").toFloat();
    coordinate[0] = root->property("coordinateX").toFloat();
    coordinate[1] = root->property("coordinateY").toFloat();
    coordinate[2] = root->property("coordinateZ").toFloat();
}

void MainWindow::navigationChanged()
{
    if (auto *root = navigationControls.data())
    {
        QMetaObject::invokeMethod(root, "setCamera", Q_ARG(QVariant, QVariant(spin)),
                                  Q_ARG(QVariant, QVariant(tilt)),
                                  Q_ARG(QVariant, QVariant(distance)));
        root->setProperty("cameraPanX", panX);
        root->setProperty("cameraPanY", panY);
        root->setProperty("cameraPanZ", panZ);
        root->setProperty("coordinateX", coordinate[0]);
        root->setProperty("coordinateY", coordinate[1]);
        root->setProperty("coordinateZ", coordinate[2]);
    }
    VulkanWindow::navigationChanged();
}

bool MainWindow::event(QEvent *event)
{
    // Apply queued panel edits before mouse tools read or modify camera data.
    if (prepareNavigation && (event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::Wheel))
        prepareNavigation();
    return VulkanWindow::event(event);
}


MainWindow::MainWindow(QWindow *parent) : VulkanWindow(parent)
{
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(16);
    clothClock.start();
    QObject::connect(timer, &QTimer::timeout, this, [this]() {
        float dt = std::min(clothClock.restart() / 1000.0f, 0.05f);
        if (!clothEnabled || !clothRunning || !isExposed()) return;
        while (dt > 0.00001f) {
            float h = std::min(dt, 1.0f/120);
            cloth.step(h, clothWind); dt -= h;
        }
        meshDirty = true; requestUpdate();
    });
    timer->start();
}
MainWindow::~MainWindow()
{
    VulkanWindow::release();
    releaseScene();
}

void MainWindow::applyBodyControls(QObject *root)
{
    makehuman::BodyParameters p;
    p.heightMeters = root->property("bodyHeight").toFloat();
    p.genderBlend = root->property("genderBlend").toFloat();
    p.chest = root->property("chest").toFloat();
    p.waist = root->property("waist").toFloat();
    p.hips = root->property("hips").toFloat();
    p.weight = root->property("weight").toFloat();
    p.armLength = root->property("armLength").toFloat();
    p.legLength = root->property("legLength").toFloat();
    p.headScale = root->property("headScale").toFloat();
    vertices = makehuman::buildHumanMesh(p);
    humanModel = true;
    shirt = buildShirt(vertices);
    for (auto &v : shirt) v.pos *= 1000.0f;
    // The existing camera and rulers use millimetres; MakeHuman emits metres.
    for (auto &v : vertices) v.pos *= 1000.0f;
    if (clothEnabled) resetCloth();
    meshDirty = true;
    requestUpdate();
}
void MainWindow::initializeScene(VkPhysicalDevice physicalDevice, VkDevice logicalDevice)
{
    physical = physicalDevice; device = logicalDevice;
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.pushConstantRangeCount = 1; info.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device, &info, nullptr, &pipelineLayout), "Mesh pipeline layout");
}
void MainWindow::calculateScene(VkCommandBuffer, VkQueue, VkFence)
{
    if (!meshDirty) return;
    auto garment = clothEnabled ? cloth.mesh() : std::vector<makehuman::MeshVertex>{};
    for (auto &v : garment) v.pos *= 1000.0f;
    if (humanModel && shirtEnabled) garment.insert(garment.end(), shirt.begin(), shirt.end());
    drawCount = uint32_t(vertices.size() + garment.size());
    const VkDeviceSize size = VkDeviceSize(drawCount) * sizeof(makehuman::MeshVertex);
    if (size > bufferCapacity) {
        vkDestroyBuffer(device, inputBuffer, nullptr); inputBuffer = VK_NULL_HANDLE;
        vkFreeMemory(device, inputMemory, nullptr); inputMemory = VK_NULL_HANDLE;
        createStorageBuffer(physical, device, size, inputBuffer, inputMemory,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        bufferCapacity = size;
    }
    if (size) {
        void *mapped;
        check(vkMapMemory(device, inputMemory, 0, size, 0, &mapped), "Map mesh");
        const size_t bodyBytes = vertices.size() * sizeof(makehuman::MeshVertex);
        std::memcpy(mapped, vertices.data(), bodyBytes);
        if (!garment.empty()) std::memcpy(static_cast<char *>(mapped)+bodyBytes,
            garment.data(), garment.size()*sizeof(makehuman::MeshVertex));
        vkUnmapMemory(device, inputMemory);
    }
    meshDirty = false;
}
void MainWindow::createScenePipeline(VkRenderPass renderPass)
{
    releaseScenePipeline();
    graphicsPipeline = meshPipeline(device, renderPass, pipelineLayout, "mesh.vert.spv", true);
}
void MainWindow::releaseScenePipeline()
{
    if (device) vkDestroyPipeline(device, graphicsPipeline, nullptr);
    graphicsPipeline = VK_NULL_HANDLE;
}
void MainWindow::releaseScene()
{
    if (!device) return;
    releaseScenePipeline();
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyBuffer(device, inputBuffer, nullptr);
    vkFreeMemory(device, inputMemory, nullptr);
    inputBuffer = VK_NULL_HANDLE; inputMemory = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE; device = VK_NULL_HANDLE;
    bufferCapacity = 0; drawCount = 0;
    meshDirty = true;
}
void MainWindow::recordScene(VkCommandBuffer command, const Matrix4 &matrix, VkExtent2D)
{
    if (!inputBuffer) return;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &inputBuffer, &offset);
    vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, matrix.data());
    vkCmdDraw(command, drawCount, 1, 0, 0);
}
std::vector<makehuman::MeshVertex> MainWindow::loadObjTriangles(const std::string& objPath) const {
    struct P3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    const std::filesystem::path inputPath(objPath);
    const std::filesystem::path fullPath = std::filesystem::absolute(inputPath);
    std::cout << "loadObjTriangles() file: " << fullPath.string() << '\n';

    std::ifstream in(fullPath);
    if (!in.is_open()) {
        std::cout << "Failed to open mesh: " << fullPath.string() << '\n';
        return {};
    }

    std::vector<P3> positions;
    std::vector<makehuman::MeshVertex> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("v ", 0) == 0) {
            std::istringstream iss(line);
            char c;
            P3 p;
            iss >> c >> p.x >> p.y >> p.z;
            positions.push_back(p);
            continue;
        }
        if (line.rfind("f ", 0) == 0) {
            std::istringstream iss(line);
            char c;
            iss >> c;
            std::vector<int> ids;
            std::string token;
            while (iss >> token) {
                const size_t slash = token.find('/');
                const std::string vStr = (slash == std::string::npos) ? token : token.substr(0, slash);
                if (vStr.empty()) {
                    continue;
                }
                int idx = std::stoi(vStr);
                if (idx > 0) {
                    ids.push_back(idx - 1);
                }
                else if (idx < 0) {
                    ids.push_back(static_cast<int>(positions.size()) + idx);
                }
            }

            if (ids.size() < 3) {
                continue;
            }

            const glm::vec3 n(0.0f, 0.0f, 1.0f);
            const glm::vec3 color(0.2f, 0.8f, 1.0f);
            for (size_t i = 1; i + 1 < ids.size(); ++i) {
                const int i0 = ids[0];
                const int i1 = ids[i];
                const int i2 = ids[i + 1];
                if (i0 < 0 || i1 < 0 || i2 < 0 ||
                    i0 >= static_cast<int>(positions.size()) ||
                    i1 >= static_cast<int>(positions.size()) ||
                    i2 >= static_cast<int>(positions.size())) {
                    continue;
                }
                out.push_back(makehuman::MeshVertex{ glm::vec3(positions[i0].x, positions[i0].y, positions[i0].z), n, color });
                out.push_back(makehuman::MeshVertex{ glm::vec3(positions[i1].x, positions[i1].y, positions[i1].z), n, color });
                out.push_back(makehuman::MeshVertex{ glm::vec3(positions[i2].x, positions[i2].y, positions[i2].z), n, color });
            }
        }
    }
    return out;
}

std::vector<makehuman::MeshVertex> MainWindow::buildCubeVertices() const {

    std::vector<makehuman::MeshVertex> v;
    glm::vec3 topColor = glm::vec3(0.0f, 0.0f, 0.5f);
    glm::vec3 bottomColor = glm::vec3(0.0f, 0.0f, 0.4f);
    glm::vec3 frontColor = glm::vec3(0.0f, 0.5f, 0.0f);
    glm::vec3 backColor = glm::vec3(0.0f, 0.4f, 0.0f);
    glm::vec3 leftColor = glm::vec3(0.4f, 0.0f, 0.0f);
    glm::vec3 rightColor = glm::vec3(0.5f, 0.0f, 0.0f);

    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        v.push_back(makehuman::MeshVertex{ a, n, color });
        v.push_back(makehuman::MeshVertex{ b, n, color });
        v.push_back(makehuman::MeshVertex{ c, n, color });
        v.push_back(makehuman::MeshVertex{ c, n, color });
        v.push_back(makehuman::MeshVertex{ d, n, color });
        v.push_back(makehuman::MeshVertex{ a, n, color });
    };

    glm::vec3 p000 = { -1.0f, -1.0f, -1.0f };
    glm::vec3 p001 = { -1.0f, -1.0f, 1.0f };
    glm::vec3 p010 = { -1.0f, 1.0f, -1.0f };
    glm::vec3 p011 = { -1.0f, 1.0f, 1.0f };
    glm::vec3 p100 = { 1.0f, -1.0f, -1.0f };
    glm::vec3 p101 = { 1.0f, -1.0f, 1.0f };
    glm::vec3 p110 = { 1.0f, 1.0f, -1.0f };
    glm::vec3 p111 = { 1.0f, 1.0f, 1.0f };

    quad(p000, p001, p101, p100, bottomColor);
    quad(p010, p110, p111, p011, topColor);
    quad(p011, p111, p101, p001, frontColor);
    quad(p010, p000, p100, p110, backColor);
    quad(p010, p011, p001, p000, leftColor);
    quad(p110, p100, p101, p111, rightColor);

    return v;
}


void MainWindow::openObjFile(const std::string &path)
{
    auto mesh = loadObjTriangles(path);
    if (mesh.empty()) throw std::runtime_error("OBJ contains no valid triangles");
    glm::vec3 low = mesh.front().pos, high = low;
    for (const auto &v : mesh) { low = glm::min(low, v.pos); high = glm::max(high, v.pos); }
    const auto size = high - low;
    const float span = std::max({size.x, size.y, size.z});
    if (span <= 0) throw std::runtime_error("OBJ has zero extent");
    for (auto &v : mesh) v.pos = (v.pos - (low + high) * 0.5f) * (1780.0f / span);
    humanModel = false; setClothState(false, clothRunning, clothWind);
    vertices = std::move(mesh); meshDirty = true; requestUpdate();
}
void MainWindow::showCubeMesh()
{
    humanModel = false; setClothState(false, clothRunning, clothWind);
    vertices = buildCubeVertices();
    for (auto &v : vertices) v.pos *= 500.0f;
    meshDirty = true; requestUpdate();
}

void MainWindow::setClothState(bool enabled, bool running, float wind)
{
    const bool wasEnabled = clothEnabled;
    clothEnabled = enabled && humanModel;
    clothRunning = running;
    clothWind = std::clamp(wind, 0.0f, 4.0f);
    if (auto *root = navigationControls.data()) root->setProperty("clothEnabled", clothEnabled);
    if (clothEnabled && !wasEnabled) resetCloth();
    clothClock.restart();
    meshDirty = true; requestUpdate();
}
void MainWindow::resetCloth()
{
    if (!humanModel || !clothEnabled) return;
    auto body = vertices;
    for (auto &v : body) v.pos /= 1000.0f;
    cloth.reset(body);
    clothClock.restart();
    meshDirty = true; requestUpdate();
}

void MainWindow::setShirtEnabled(bool enabled)
{
    shirtEnabled = enabled;
    meshDirty = true;
    requestUpdate();
}
