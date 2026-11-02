#include "../include/engine_sim_application.h"

#include "../include/piston_engine_simulator.h"
#include "../include/cam_lobe.h"
#include "../include/valvetrain.h"
#include "../include/piston_object.h"
#include "../include/connecting_rod_object.h"
#include "../include/constants.h"
#include "../include/units.h"
#include "../include/crankshaft_object.h"
#include "../include/cylinder_bank_object.h"
#include "../include/cylinder_head_object.h"
#include "../include/ui_button.h"
#include "../include/combustion_chamber_object.h"
#include "../include/csv_io.h"
#include "../include/exhaust_system.h"
#include "../include/feedback_comb_filter.h"
#include "../include/utilities.h"

#include "../scripting/include/compiler.h"

#include <chrono>
#include <fstream>
#include <map>
#include <stdlib.h>
#include <sstream>

#if ATG_ENGINE_SIM_DISCORD_ENABLED
#include "../discord/Discord.h"
#endif

std::string EngineSimApplication::s_buildVersion = "0.1.12a";

EngineSimApplication::EngineSimApplication() {
    m_assetPath = "";

    m_geometryVertexBuffer = nullptr;
    m_geometryIndexBuffer = nullptr;

    m_paused = false;
    m_recording = false;
    m_screenResolutionIndex = 0;
    for (int i = 0; i < ScreenResolutionHistoryLength; ++i) {
        m_screenResolution[i][0] = m_screenResolution[i][1] = 0;
    }

    m_background = ysColor::srgbiToLinear(0x0E1012);
    m_foreground = ysColor::srgbiToLinear(0xFFFFFF);
    m_shadow = ysColor::srgbiToLinear(0x0E1012);
    m_highlight1 = ysColor::srgbiToLinear(0xEF4545);
    m_highlight2 = ysColor::srgbiToLinear(0xFFFFFF);
    m_pink = ysColor::srgbiToLinear(0xF394BE);
    m_red = ysColor::srgbiToLinear(0xEE4445);
    m_orange = ysColor::srgbiToLinear(0xF4802A);
    m_yellow = ysColor::srgbiToLinear(0xFDBD2E);
    m_blue = ysColor::srgbiToLinear(0x77CEE0);
    m_green = ysColor::srgbiToLinear(0xBDD869);

    m_displayHeight = (float)units::distance(2.0, units::foot);
    m_outputAudioBuffer = nullptr;
    m_audioSource = nullptr;

    m_torque = 0;
    m_dynoSpeed = 0;

    m_simulator = nullptr;
    m_engineView = nullptr;
    m_rightGaugeCluster = nullptr;
    m_temperatureGauge = nullptr;
    m_oscCluster = nullptr;
    m_performanceCluster = nullptr;
    m_tuningCluster = nullptr;
    m_loadSimulationCluster = nullptr;
    m_mixerCluster = nullptr;
    m_infoCluster = nullptr;
    m_iceEngine = nullptr;
    m_mainRenderTarget = nullptr;

    m_vehicle = nullptr;
    m_transmission = nullptr;

    m_oscillatorSampleOffset = 0;
    m_gameWindowHeight = 256;
    m_screenWidth = 256;
    m_screenHeight = 256;
    m_screen = 0;
    m_viewParameters.Layer0 = 0;
    m_viewParameters.Layer1 = 0;

    m_displayAngle = 0.0f;
}

EngineSimApplication::~EngineSimApplication() {
    /* void */
}

void EngineSimApplication::initialize(void *instance, ysContextObject::DeviceAPI api) {
    dbasic::Path modulePath = dbasic::GetModulePath();
    dbasic::Path confPath = modulePath.Append("delta.conf");

    std::string enginePath = "../dependencies/submodules/delta-studio/engines/basic";
    m_assetPath = "../assets";
    if (confPath.Exists()) {
        std::fstream confFile(confPath.ToString(), std::ios::in);

        std::getline(confFile, enginePath);
        std::getline(confFile, m_assetPath);
        enginePath = modulePath.Append(enginePath).ToString();
        m_assetPath = modulePath.Append(m_assetPath).ToString();

        confFile.close();
    }

    m_engine.GetConsole()->SetDefaultFontDirectory(enginePath + "/fonts/");

    const std::string shaderPath = enginePath + "/shaders/";
    const std::string winTitle = "Engine Sim | AngeTheGreat | v" + s_buildVersion;
    dbasic::DeltaEngine::GameEngineSettings settings;
    settings.API = api;
    settings.DepthBuffer = false;
    settings.Instance = instance;
    settings.ShaderDirectory = shaderPath.c_str();
    settings.WindowTitle = winTitle.c_str();
    settings.WindowPositionX = 0;
    settings.WindowPositionY = 0;
    settings.WindowStyle = ysWindow::WindowStyle::Windowed;
    settings.WindowWidth = 1920;
    settings.WindowHeight = 1080;

    m_engine.CreateGameWindow(settings);

    m_engine.GetDevice()->CreateSubRenderTarget(
        &m_mainRenderTarget,
        m_engine.GetScreenRenderTarget(),
        0,
        0,
        0,
        0);

    m_engine.InitializeShaderSet(&m_shaderSet);
    m_shaders.Initialize(
        &m_shaderSet,
        m_mainRenderTarget,
        m_engine.GetScreenRenderTarget(),
        m_engine.GetDefaultShaderProgram(),
        m_engine.GetDefaultInputLayout());
    m_engine.InitializeConsoleShaders(&m_shaderSet);
    m_engine.SetShaderSet(&m_shaderSet);

    m_shaders.SetClearColor(ysColor::srgbiToLinear(0x34, 0x98, 0xdb));

    m_assetManager.SetEngine(&m_engine);

    m_engine.GetDevice()->CreateIndexBuffer(
        &m_geometryIndexBuffer, sizeof(unsigned short) * 200000, nullptr);
    m_engine.GetDevice()->CreateVertexBuffer(
        &m_geometryVertexBuffer, sizeof(dbasic::Vertex) * 100000, nullptr);

    m_geometryGenerator.initialize(100000, 200000);

    initialize();
}

void EngineSimApplication::initialize() {
    m_shaders.SetClearColor(ysColor::srgbiToLinear(0x34, 0x98, 0xdb));
    m_assetManager.CompileInterchangeFile((m_assetPath + "/assets").c_str(), 1.0f, true);
    m_assetManager.LoadSceneFile((m_assetPath + "/assets").c_str(), true);

    m_textRenderer.SetEngine(&m_engine);
    m_textRenderer.SetRenderer(m_engine.GetUiRenderer());
    m_textRenderer.SetFont(m_engine.GetConsole()->GetFont());

    loadScript();

    m_audioBuffer.initialize(44100, 44100);
    m_audioBuffer.m_writePointer = (int)(44100 * 0.1);

    ysAudioParameters params;
    params.m_bitsPerSample = 16;
    params.m_channelCount = 1;
    params.m_sampleRate = 44100;
    m_outputAudioBuffer =
        m_engine.GetAudioDevice()->CreateBuffer(&params, 44100);

    m_audioSource = m_engine.GetAudioDevice()->CreateSource(m_outputAudioBuffer);
    m_audioSource->SetMode((m_simulator->getEngine() != nullptr)
        ? ysAudioSource::Mode::Loop
        : ysAudioSource::Mode::Stop);
    m_audioSource->SetPan(0.0f);
    m_audioSource->SetVolume(1.0f);

#ifdef ATG_ENGINE_SIM_DISCORD_ENABLED
    // Create a global instance of discord-rpc
    CDiscord::CreateInstance();

    // Enable it, this needs to be set via a config file of some sort. 
    GetDiscordManager()->SetUseDiscord(true);
    DiscordRichPresence passMe = { 0 };

    std::string engineName = (m_iceEngine != nullptr)
        ? m_iceEngine->getName()
        : "Broken Engine";

    GetDiscordManager()->SetStatus(passMe, engineName, s_buildVersion);
#endif /* ATG_ENGINE_SIM_DISCORD_ENABLED */
}

void EngineSimApplication::process(float frame_dt) {
    frame_dt = static_cast<float>(clamp(frame_dt, 1 / 200.0f, 1 / 30.0f));

    double speed = 1.0 / 1.0;
    if (m_engine.IsKeyDown(ysKey::Code::N1)) {
        speed = 1 / 10.0;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::N2)) {
        speed = 1 / 100.0;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::N3)) {
        speed = 1 / 200.0;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::N4)) {
        speed = 1 / 500.0;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::N5)) {
        speed = 1 / 1000.0;
    }

    if (m_engine.IsKeyDown(ysKey::Code::F1)) {
        m_displayAngle += frame_dt * 1.0f;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::F2)) {
        m_displayAngle -= frame_dt * 1.0f;
    }
    else if (m_engine.ProcessKeyDown(ysKey::Code::F3)) {
        m_displayAngle = 0.0f;
    }

    m_simulator->setSimulationSpeed(speed);

    const double avgFramerate = clamp(m_engine.GetAverageFramerate(), 30.0f, 1000.0f);
    m_simulator->startFrame(1 / avgFramerate);

    auto proc_t0 = std::chrono::steady_clock::now();
    const int iterationCount = m_simulator->getFrameIterationCount();
    while (m_simulator->simulateStep()) {
        m_oscCluster->sample();
        m_dynoSweep.sample(m_simulator, m_iceEngine);
    }

    auto proc_t1 = std::chrono::steady_clock::now();

    m_simulator->endFrame();

    auto duration = proc_t1 - proc_t0;
    if (iterationCount > 0) {
        m_performanceCluster->addTimePerTimestepSample(
            (duration.count() / 1E9) / iterationCount);
    }

    const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
    const SampleOffset writePosition = m_audioBuffer.m_writePointer;

    SampleOffset targetWritePosition =
        m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
    SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);

    SampleOffset currentLead = m_audioBuffer.offsetDelta(safeWritePosition, writePosition);
    SampleOffset newLead = m_audioBuffer.offsetDelta(safeWritePosition, targetWritePosition);

    if (currentLead > 44100 * 0.5) {
        m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
        currentLead = m_audioBuffer.offsetDelta(safeWritePosition, m_audioBuffer.m_writePointer);
        maxWrite = m_audioBuffer.offsetDelta(m_audioBuffer.m_writePointer, targetWritePosition);
    }

    if (currentLead > newLead) {
        maxWrite = 0;
    }

    int16_t *samples = new int16_t[maxWrite];
    const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);

    for (SampleOffset i = 0; i < (SampleOffset)readSamples && i < maxWrite; ++i) {
        const int16_t sample = samples[i];
        if (m_oscillatorSampleOffset % 4 == 0) {
            m_oscCluster->getAudioWaveformOscilloscope()->addDataPoint(
                m_oscillatorSampleOffset,
                sample / (float)(INT16_MAX));
        }

        m_audioBuffer.writeSample(sample, m_audioBuffer.m_writePointer, (int)i);

        m_oscillatorSampleOffset = (m_oscillatorSampleOffset + 1) % (44100 / 10);
    }

    delete[] samples;

    if (readSamples > 0) {
        SampleOffset size0, size1;
        void *data0, *data1;
        m_audioSource->LockBufferSegment(
            m_audioBuffer.m_writePointer, readSamples, &data0, &size0, &data1, &size1);

        m_audioBuffer.copyBuffer(
            reinterpret_cast<int16_t *>(data0), m_audioBuffer.m_writePointer, size0);
        m_audioBuffer.copyBuffer(
            reinterpret_cast<int16_t *>(data1),
            m_audioBuffer.getBufferIndex(m_audioBuffer.m_writePointer, size0),
            size1);

        m_audioSource->UnlockBufferSegments(data0, size0, data1, size1);
        m_audioBuffer.commitBlock(readSamples);
    }

    m_performanceCluster->addInputBufferUsageSample(
        (double)m_simulator->getSynthesizerInputLatency() / m_simulator->getSynthesizerInputLatencyTarget());
    m_performanceCluster->addAudioLatencySample(
        m_audioBuffer.offsetDelta(m_audioSource->GetCurrentWritePosition(), m_audioBuffer.m_writePointer) / (44100 * 0.1));
}

void EngineSimApplication::render() {
    for (SimulationObject *object : m_objects) {
        object->generateGeometry();
    }

    m_viewParameters.Sublayer = 0;
    for (SimulationObject *object : m_objects) {
        object->render(&getViewParameters());
    }

    m_viewParameters.Sublayer = 1;
    for (SimulationObject *object : m_objects) {
        object->render(&getViewParameters());
    }

    m_viewParameters.Sublayer = 2;
    for (SimulationObject *object : m_objects) {
        object->render(&getViewParameters());
    }

    m_uiManager.render();
}

float EngineSimApplication::pixelsToUnits(float pixels) const {
    const float f = m_displayHeight / m_engineView->m_bounds.height();
    return pixels * f;
}

float EngineSimApplication::unitsToPixels(float units) const {
    const float f = m_engineView->m_bounds.height() / m_displayHeight;
    return units * f;
}

void EngineSimApplication::run() {
    while (true) {
        m_engine.StartFrame();

        if (!m_engine.IsOpen()) break;
        if (m_engine.ProcessKeyDown(ysKey::Code::Escape)) {
            break;
        }

        if (m_engine.ProcessKeyDown(ysKey::Code::Return)) {
            reloadScript();
        }

        if (m_engine.ProcessKeyDown(ysKey::Code::F5)) {
            toggleLiveReload();
        }

        updateLiveReload(m_engine.GetFrameLength());

        if (m_iceEngine != nullptr && m_infoCluster != nullptr && m_iceEngine->hasHighLift()) {
            const bool high = m_iceEngine->isHighLiftActive();
            if (high != m_lastHighLift) {
                m_lastHighLift = high;
                m_infoCluster->setLogMessage(
                    std::string(high ? "CAM: HIGH LIFT @ " : "CAM: LOW LIFT @ ")
                    + std::to_string((int)units::toRpm(std::fabs(m_iceEngine->getSpeed()))) + " rpm");
            }
        }

        if (m_liveReload) {
            m_liveServer.poll([this](const std::string &method, const std::string &path, const std::string &body) {
                return handleLiveRoute(method, path, body);
            });
        }

        if (m_engine.ProcessKeyDown(ysKey::Code::Tab)) {
            m_screen++;
            if (m_screen > 2) m_screen = 0;
        }

        if (m_engine.ProcessKeyDown(ysKey::Code::F)) {
            if (m_engine.GetGameWindow()->GetWindowStyle() != ysWindow::WindowStyle::Fullscreen) {
                m_engine.GetGameWindow()->SetWindowStyle(ysWindow::WindowStyle::Fullscreen);
                m_infoCluster->setLogMessage("Entered fullscreen mode");
            }
            else {
                m_engine.GetGameWindow()->SetWindowStyle(ysWindow::WindowStyle::Windowed);
                m_infoCluster->setLogMessage("Exited fullscreen mode");
            }
        }

        m_gameWindowHeight = m_engine.GetGameWindow()->GetGameHeight();
        m_screenHeight = m_engine.GetGameWindow()->GetScreenHeight();
        m_screenWidth = m_engine.GetGameWindow()->GetScreenWidth();

        updateScreenSizeStability();

        processEngineInput();
        m_dynoSweep.update(m_engine.GetFrameLength(), m_iceEngine, m_simulator);

        if (m_engine.ProcessKeyDown(ysKey::Code::Insert) &&
            m_engine.GetGameWindow()->IsActive()) {
            if (!isRecording() && readyToRecord()) {
                startRecording();
            }
            else if (isRecording()) {
                stopRecording();
            }
        }

        if (isRecording() && !readyToRecord()) {
            stopRecording();
        }

        if (!m_paused || m_engine.ProcessKeyDown(ysKey::Code::Right)) {
            process(m_engine.GetFrameLength());
        }

        m_uiManager.update(m_engine.GetFrameLength());

        renderScene();

        m_engine.EndFrame();

        if (isRecording()) {
            recordFrame();
        }
    }

    if (isRecording()) {
        stopRecording();
    }

    m_simulator->endAudioRenderingThread();
}

void EngineSimApplication::destroy() {
    m_shaderSet.Destroy();

    m_engine.GetDevice()->DestroyGPUBuffer(m_geometryVertexBuffer);
    m_engine.GetDevice()->DestroyGPUBuffer(m_geometryIndexBuffer);

    m_assetManager.Destroy();
    m_engine.Destroy();

    m_simulator->destroy();
    m_audioBuffer.destroy();
}

void EngineSimApplication::loadEngine(
    Engine *engine,
    Vehicle *vehicle,
    Transmission *transmission)
{
    m_dynoSweep.abort("engine reloaded", nullptr, nullptr);

    destroyObjects();

    if (m_simulator != nullptr) {
        m_simulator->releaseSimulation();
        delete m_simulator;
    }

    if (m_vehicle != nullptr) {
        delete m_vehicle;
        m_vehicle = nullptr;
    }

    if (m_transmission != nullptr) {
        delete m_transmission;
        m_transmission = nullptr;
    }

    if (m_iceEngine != nullptr) {
        m_iceEngine->destroy();
        delete m_iceEngine;
    }

    m_iceEngine = engine;
    m_vehicle = vehicle;
    m_transmission = transmission;

    m_simulator = engine->createSimulator(vehicle, transmission);

    if (engine == nullptr || vehicle == nullptr || transmission == nullptr) {
        m_iceEngine = nullptr;
        m_viewParameters.Layer1 = 0;

        return;
    }

    createObjects(engine);

    m_viewParameters.Layer1 = engine->getMaxDepth();
    engine->calculateDisplacement();

    m_simulator->setSimulationFrequency(engine->getSimulationFrequency());

    Synthesizer::AudioParameters audioParams = m_simulator->synthesizer().getAudioParameters();
    audioParams.inputSampleNoise = static_cast<float>(engine->getInitialJitter());
    audioParams.airNoise = static_cast<float>(engine->getInitialNoise());
    audioParams.dF_F_mix = static_cast<float>(engine->getInitialHighFrequencyGain());
    m_simulator->synthesizer().setAudioParameters(audioParams);

    for (int i = 0; i < engine->getExhaustSystemCount(); ++i) {
        ImpulseResponse *response = engine->getExhaustSystem(i)->getImpulseResponse();

        ysWindowsAudioWaveFile waveFile;
        waveFile.OpenFile(response->getFilename().c_str());
        waveFile.InitializeInternalBuffer(waveFile.GetSampleCount());
        waveFile.FillBuffer(0);
        waveFile.CloseFile();

        m_simulator->synthesizer().initializeImpulseResponse(
            reinterpret_cast<const int16_t *>(waveFile.GetBuffer()),
            waveFile.GetSampleCount(),
            response->getVolume(),
            i
        );

        waveFile.DestroyInternalBuffer();
    }

    m_simulator->startAudioRenderingThread();
}

void EngineSimApplication::drawGenerated(
    const GeometryGenerator::GeometryIndices &indices,
    int layer)
{
    drawGenerated(indices, layer, m_shaders.GetRegularFlags());
}

void EngineSimApplication::drawGeneratedUi(
    const GeometryGenerator::GeometryIndices &indices,
    int layer)
{
    drawGenerated(indices, layer, m_shaders.GetUiFlags());
}

void EngineSimApplication::drawGenerated(
    const GeometryGenerator::GeometryIndices &indices,
    int layer,
    dbasic::StageEnableFlags flags)
{
    m_engine.DrawGeneric(
        flags,
        m_geometryIndexBuffer,
        m_geometryVertexBuffer,
        sizeof(dbasic::Vertex),
        indices.BaseIndex,
        indices.BaseVertex,
        indices.FaceCount,
        false,
        layer);
}

void EngineSimApplication::configure(const ApplicationSettings &settings) {
    m_applicationSettings = settings;

    if (settings.startFullscreen) {
        m_engine.GetGameWindow()->SetWindowStyle(ysWindow::WindowStyle::Fullscreen);
    }

    m_background = ysColor::srgbiToLinear(m_applicationSettings.colorBackground);
    m_foreground = ysColor::srgbiToLinear(m_applicationSettings.colorForeground);
    m_shadow = ysColor::srgbiToLinear(m_applicationSettings.colorShadow);
    m_highlight1 = ysColor::srgbiToLinear(m_applicationSettings.colorHighlight1);
    m_highlight2 = ysColor::srgbiToLinear(m_applicationSettings.colorHighlight2);
    m_pink = ysColor::srgbiToLinear(m_applicationSettings.colorPink);
    m_red = ysColor::srgbiToLinear(m_applicationSettings.colorRed);
    m_orange = ysColor::srgbiToLinear(m_applicationSettings.colorOrange);
    m_yellow = ysColor::srgbiToLinear(m_applicationSettings.colorYellow);
    m_blue = ysColor::srgbiToLinear(m_applicationSettings.colorBlue);
    m_green = ysColor::srgbiToLinear(m_applicationSettings.colorGreen);
}

void EngineSimApplication::createObjects(Engine *engine) {
    for (int i = 0; i < engine->getCylinderCount(); ++i) {
        ConnectingRodObject *rodObject = new ConnectingRodObject;
        rodObject->initialize(this);
        rodObject->m_connectingRod = engine->getConnectingRod(i);
        m_objects.push_back(rodObject);

        PistonObject *pistonObject = new PistonObject;
        pistonObject->initialize(this);
        pistonObject->m_piston = engine->getPiston(i);
        m_objects.push_back(pistonObject);

        CombustionChamberObject *ccObject = new CombustionChamberObject;
        ccObject->initialize(this);
        ccObject->m_chamber = m_iceEngine->getChamber(i);
        m_objects.push_back(ccObject);
    }

    for (int i = 0; i < engine->getCrankshaftCount(); ++i) {
        CrankshaftObject *crankshaftObject = new CrankshaftObject;
        crankshaftObject->initialize(this);
        crankshaftObject->m_crankshaft = engine->getCrankshaft(i);
        m_objects.push_back(crankshaftObject);
    }

    for (int i = 0; i < engine->getCylinderBankCount(); ++i) {
        CylinderBankObject *cbObject = new CylinderBankObject;
        cbObject->initialize(this);
        cbObject->m_bank = engine->getCylinderBank(i);
        cbObject->m_head = engine->getHead(i);
        m_objects.push_back(cbObject);

        CylinderHeadObject *chObject = new CylinderHeadObject;
        chObject->initialize(this);
        chObject->m_head = engine->getHead(i);
        chObject->m_engine = engine;
        m_objects.push_back(chObject);
    }
}

void EngineSimApplication::destroyObjects() {
    for (SimulationObject *object : m_objects) {
        object->destroy();
        delete object;
    }

    m_objects.clear();
}

const SimulationObject::ViewParameters &
    EngineSimApplication::getViewParameters() const
{
    return m_viewParameters;
}

// ENGINE_SIM_LIVE_ENTRY / ENGINE_SIM_LIVE_FILE let a second instance (e.g. for tests) use its own files
static std::string liveEntryScript() {
    const char *value = std::getenv("ENGINE_SIM_LIVE_ENTRY");
    return (value != nullptr && value[0] != 0) ? value : "../assets/live_entry.mr";
}

static std::string liveEngineFile() {
    const char *value = std::getenv("ENGINE_SIM_LIVE_FILE");
    return (value != nullptr && value[0] != 0) ? value : "../assets/engines/custom/live_engine.mr";
}
static constexpr int LiveServerPort = 8765;

// Header lines written by tools/engine-builder.html:
//   // builder:structure <hash>         hash of everything that needs a full reload
//   // builder:live <key> <values...>   values that can be changed while the engine runs
struct LiveHeader {
    std::string structure;
    std::map<std::string, std::string> values;
};

static LiveHeader readLiveHeader(const char *path) {
    LiveHeader header;
    std::ifstream file(path);
    std::string line;
    for (int i = 0; i < 40 && std::getline(file, line); ++i) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const std::string structureTag = "// builder:structure ";
        const std::string liveTag = "// builder:live ";
        if (line.compare(0, structureTag.size(), structureTag) == 0) {
            header.structure = line.substr(structureTag.size());
        }
        else if (line.compare(0, liveTag.size(), liveTag) == 0) {
            const std::string rest = line.substr(liveTag.size());
            const size_t space = rest.find(' ');
            if (space != std::string::npos) {
                header.values[rest.substr(0, space)] = rest.substr(space + 1);
            }
        }
    }

    return header;
}

bool EngineSimApplication::loadScript(const std::string &path, bool keepOnFailure) {
    Engine *engine = nullptr;
    Vehicle *vehicle = nullptr;
    Transmission *transmission = nullptr;

#ifdef ATG_ENGINE_SIM_PIRANHA_ENABLED
    es_script::Compiler compiler;
    compiler.initialize();
    const bool compiled = compiler.compile(path);
    if (compiled) {
        const es_script::Compiler::Output output = compiler.execute();

        if (keepOnFailure && output.engine == nullptr) {
            // Script compiled but did not produce an engine: keep the running one
            compiler.destroy();
            return false;
        }

        configure(output.applicationSettings);

        engine = output.engine;
        vehicle = output.vehicle;
        transmission = output.transmission;
    }
    else {
        engine = nullptr;
        vehicle = nullptr;
        transmission = nullptr;

        if (keepOnFailure) {
            // Syntax errors are listed in error_log.log, the running engine stays untouched
            compiler.destroy();
            return false;
        }
    }

    compiler.destroy();
#endif /* ATG_ENGINE_SIM_PIRANHA_ENABLED */

    if (vehicle == nullptr) {
        Vehicle::Parameters vehParams;
        vehParams.mass = units::mass(1597, units::kg);
        vehParams.diffRatio = 3.42;
        vehParams.tireRadius = units::distance(10, units::inch);
        vehParams.dragCoefficient = 0.25;
        vehParams.crossSectionArea = units::distance(6.0, units::foot) * units::distance(6.0, units::foot);
        vehParams.rollingResistance = 2000.0;
        vehicle = new Vehicle;
        vehicle->initialize(vehParams);
    }

    if (transmission == nullptr) {
        const double gearRatios[] = { 2.97, 2.07, 1.43, 1.00, 0.84, 0.56 };
        Transmission::Parameters tParams;
        tParams.GearCount = 6;
        tParams.GearRatios = gearRatios;
        tParams.MaxClutchTorque = units::torque(1000.0, units::ft_lb);
        transmission = new Transmission;
        transmission->initialize(tParams);
    }

    loadEngine(engine, vehicle, transmission);
    refreshUserInterface();

    return true;
}

bool EngineSimApplication::reloadScript() {
    m_audioSource->SetMode(ysAudioSource::Mode::Stop);

    const bool ok = m_liveReload
        ? loadScript(liveEntryScript(), true)
        : loadScript();

    if (m_liveReload && ok) {
        m_liveStructure = readLiveHeader(liveEngineFile().c_str()).structure;
        m_lastCamLow.clear();
        m_lastCamHigh.clear();
        m_lastHighLift = false;

        // A fresh engine only knows what the script says: add the live values (e.g. the ignition map)
        if (m_iceEngine != nullptr) applyLiveTuning(liveEngineFile());
    }

    if (m_simulator->getEngine() != nullptr) {
        m_audioSource->SetMode(ysAudioSource::Mode::Loop);
    }

    if (m_liveReload && m_infoCluster != nullptr) {
        m_infoCluster->setLogMessage(ok
            ? "Live reload: engine loaded"
            : "Live reload failed - see error_log.log (previous engine kept)");
    }

    return ok;
}

void EngineSimApplication::toggleLiveReload() {
    m_liveReload = !m_liveReload;
    m_liveStructure.clear();
    m_livePending = false;
    m_livePollTimer = 0.0f;

    std::error_code ec;
    m_liveStamp = std::filesystem::last_write_time(liveEngineFile().c_str(), ec);

    const bool ok = reloadScript();

    bool listening = false;
    if (m_liveReload) {
        // ENGINE_SIM_LIVE_PORT lets a second instance (e.g. for tests) use another port
        const char *portOverride = std::getenv("ENGINE_SIM_LIVE_PORT");
        const int port = (portOverride != nullptr && std::atoi(portOverride) > 0)
            ? std::atoi(portOverride)
            : LiveServerPort;
        listening = m_liveServer.start(port);
    }
    else {
        m_liveServer.stop();
    }

    if (m_infoCluster != nullptr && (ok || !m_liveReload)) {
        m_infoCluster->setLogMessage(m_liveReload
            ? (listening
                ? "Live reload ON - engine builder can connect (port " + std::to_string(m_liveServer.getPort()) + ")"
                : "Live reload ON - file watch only (live port busy)")
            : "Live reload OFF");
    }
}

void EngineSimApplication::updateLiveReload(float dt) {
    if (!m_liveReload) return;

    m_livePollTimer += dt;
    if (m_livePollTimer < 0.25f) return;
    m_livePollTimer = 0.0f;

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(liveEngineFile().c_str(), ec);
    if (ec) return;

    if (stamp != m_liveStamp) {
        // Wait for one more poll so a half-written file is not picked up
        m_liveStamp = stamp;
        m_livePending = true;
    }
    else if (m_livePending) {
        m_livePending = false;

        // Only values marked "live" changed -> patch the running engine, no restart
        const LiveHeader header = readLiveHeader(liveEngineFile().c_str());
        if (!header.structure.empty()
            && header.structure == m_liveStructure
            && m_iceEngine != nullptr)
        {
            applyLiveTuning(liveEngineFile().c_str());
        }
        else {
            reloadScript();
        }
    }
}

std::string EngineSimApplication::applyLiveTuning(const std::string &path) {
    const LiveHeader header = readLiveHeader(path.c_str());
    std::string applied;

    auto number = [&](const char *key, double *out) {
        const auto it = header.values.find(key);
        if (it == header.values.end()) return false;
        char *end = nullptr;
        *out = std::strtod(it->second.c_str(), &end);
        return end != it->second.c_str();
    };

    double v = 0;

    if (number("idle_throttle_plate_position", &v)) {
        for (int i = 0; i < m_iceEngine->getIntakeCount(); ++i) {
            m_iceEngine->getIntake(i)->setIdleThrottlePlatePosition(v);
        }
        applied += " idle_throttle=" + std::to_string(v).substr(0, 6);
    }

    if (number("rev_limit_rpm", &v)) {
        m_iceEngine->getIgnitionModule()->setRevLimit(units::rpm(v));
        applied += " rev_limit=" + std::to_string((int)v);
    }

    if (number("starter_torque_nm", &v)) {
        m_simulator->m_starterMotor.m_maxTorque = units::torque(v, units::Nm);
        applied += " starter=" + std::to_string((int)v) + "Nm";
    }

    if (number("friction_torque_nm", &v)) {
        PistonEngineSimulator *piston = dynamic_cast<PistonEngineSimulator *>(m_simulator);
        if (piston != nullptr) {
            for (int i = 0; i < m_iceEngine->getCrankshaftCount(); ++i) {
                m_iceEngine->getCrankshaft(i)->setFrictionTorque(units::torque(v, units::Nm));
                piston->setCrankshaftFrictionTorque(i, units::torque(v, units::Nm));
            }
            applied += " friction=" + std::to_string(v).substr(0, 5) + "Nm";
        }
    }

    // Cam profiles. cam_low / cam_high: intake lift [mm], intake duration [deg at 0.050 in], exhaust lift [mm],
    // exhaust duration [deg]; cam_switch: speed [rpm], hysteresis [rpm], minimum throttle [%], minimum manifold pressure [kPa]
    {
        const auto low = header.values.find("cam_low");
        const auto high = header.values.find("cam_high");
        const std::string lowText = low != header.values.end() ? low->second : "";
        const std::string highText = high != header.values.end() ? high->second : "";

        if (lowText != m_lastCamLow || highText != m_lastCamHigh) {
            auto parse = [](const std::string &text) {
                std::vector<double> values;
                std::stringstream ss(text);
                double value;
                while (ss >> value) values.push_back(value);
                return values;
            };
            auto make = [](double liftMm, double durationDeg) {
                return generateHarmonicCamLobe(durationDeg * units::deg, 1.0, units::distance(liftMm, units::mm), 100);
            };

            const std::vector<double> l = parse(lowText), h = parse(highText);
            Function *il = nullptr, *el = nullptr, *ih = nullptr, *eh = nullptr;
            if (l.size() == 4) { il = make(l[0], l[1]); el = make(l[2], l[3]); }
            if (h.size() == 4) { ih = make(h[0], h[1]); eh = make(h[2], h[3]); }

            const bool valid = (l.size() != 4 || (il && el)) && (h.size() != 4 || (ih && eh));
            std::vector<Function *> created;
            for (Function *f : { il, el, ih, eh }) if (f != nullptr) created.push_back(f);

            if (valid && !created.empty()) {
                for (int i = 0; i < m_iceEngine->getCylinderBankCount(); ++i) {
                    m_iceEngine->getHead(i)->getValvetrain()->setLobeProfiles(il, el, ih, eh);
                }

                m_liveLobes.push_back(created);
                while (m_liveLobes.size() > 3) {                    // the engine view picks up the new profiles at once
                    for (Function *f : m_liveLobes.front()) { f->destroy(); delete f; }
                    m_liveLobes.pop_front();
                }

                applied += " cam_lobes";
            }
            else {
                for (Function *f : created) { f->destroy(); delete f; }
                if (!created.empty() || l.size() == 4 || h.size() == 4) applied += " cam_lobes_INVALID";
            }

            m_lastCamLow = lowText;
            m_lastCamHigh = highText;
        }

        const auto sw = header.values.find("cam_switch");
        if (sw != header.values.end()) {
            std::stringstream ss(sw->second);
            double rpm = 0, hysteresis = 0, throttle = 0, manifold = 0;
            if (ss >> rpm >> hysteresis >> throttle >> manifold) {
                for (int i = 0; i < m_iceEngine->getCylinderBankCount(); ++i) {
                    m_iceEngine->getHead(i)->getValvetrain()->setSwitching(
                        units::rpm(rpm), units::rpm(hysteresis), throttle / 100.0, manifold * 1000.0);
                }
                applied += " cam_switch=" + std::to_string((int)rpm);
            }
        }
    }

    if (number("fuel_octane", &v)) {
        m_iceEngine->getFuel()->setOctane(v);
        applied += " octane=" + std::to_string((int)v);
    }

    if (number("knock_damage", &v)) {
        m_iceEngine->setKnockDamageEnabled(v > 0.5);
        applied += v > 0.5 ? " knock_damage=on" : " knock_damage=off";
    }

    if (number("engine_repair", &v)) {
        // every click on "repair" in the builder raises the counter
        const int counter = (int)v;
        if (m_lastRepairCounter >= 0 && counter > m_lastRepairCounter) {
            m_iceEngine->repairKnockDamage();
            applied += " repaired";
        }

        m_lastRepairCounter = counter;
    }

    const auto curve = header.values.find("timing_curve");
    if (curve != header.values.end()) {
        // format: "<rpm>:<deg> <rpm>:<deg> ..."
        std::vector<std::pair<double, double>> samples;
        std::stringstream ss(curve->second);
        std::string token;
        while (ss >> token) {
            const size_t colon = token.find(':');
            if (colon == std::string::npos) continue;
            samples.push_back({
                std::atof(token.substr(0, colon).c_str()),
                std::atof(token.substr(colon + 1).c_str()) });
        }

        if (!samples.empty()) {
            Function *next = new Function;
            next->initialize((int)samples.size(), units::rpm(1000.0)); // same filter radius as the generated script
            for (const auto &sample : samples) {
                next->addSample(units::rpm(sample.first), sample.second * units::deg);
            }

            Function *previous = m_iceEngine->getIgnitionModule()->getTimingCurve();
            m_iceEngine->getIgnitionModule()->setTimingCurve(next);

            if (m_liveTimingCurve != nullptr && previous == m_liveTimingCurve) {
                m_liveTimingCurve->destroy();
                delete m_liveTimingCurve;
            }

            m_liveTimingCurve = next;
            applied += " timing_curve";
        }
    }

    // Ignition map (advance over speed and manifold pressure): replaces the curve while it is present
    auto numbers = [&](const char *key, double scale) {
        std::vector<double> out;
        const auto it = header.values.find(key);
        if (it == header.values.end()) return out;

        std::stringstream ss(it->second);
        double value;
        while (ss >> value) out.push_back(value * scale);
        return out;
    };

    const std::vector<double> mapSpeeds = numbers("map_rpm", 1.0);                 // rpm
    const std::vector<double> mapLoads = numbers("map_load", 1000.0);              // kPa -> Pa
    const std::vector<double> mapAdvance = numbers("map_adv", units::deg);         // deg -> rad
    IgnitionModule *ignition = m_iceEngine->getIgnitionModule();
    if (!mapSpeeds.empty() && !mapLoads.empty() && mapAdvance.size() == mapSpeeds.size() * mapLoads.size()) {
        ignition->setTimingMap(mapSpeeds, mapLoads, mapAdvance);
        applied += ignition->hasTimingMap() ? " ignition_map" : " ignition_map_INVALID";
    }
    else if (ignition->hasTimingMap()) {
        ignition->clearTimingMap();
        applied += " ignition_map_off";
    }

    if (m_infoCluster != nullptr) {
        m_infoCluster->setLogMessage("Live (no restart):" + applied);
    }

    return applied;
}

static std::string jsonEscape(const std::string &in) {
    std::string out;
    for (const char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\t': out += "\\t"; break;
            default: out += ((unsigned char)c < 0x20) ? ' ' : c;
        }
    }
    return out;
}

// Called on the main thread for every engine pushed by the engine builder
std::string EngineSimApplication::handleLiveRequest(const std::string &body) {
    {
        std::ofstream file(liveEngineFile().c_str(), std::ios::binary | std::ios::trunc);
        file.write(body.data(), (std::streamsize)body.size());
        if (!file.good()) {
            return "{\"ok\":false,\"error\":\"could not write assets/engines/custom/live_engine.mr\"}";
        }
    }

    std::string result;
    const LiveHeader header = readLiveHeader(liveEngineFile().c_str());

    if (!header.structure.empty() && header.structure == m_liveStructure && m_iceEngine != nullptr) {
        const std::string applied = applyLiveTuning(liveEngineFile().c_str());
        result = "{\"ok\":true,\"mode\":\"live\",\"message\":\"" + jsonEscape(applied) + "\"}";
    }
    else {
        const bool ok = reloadScript();
        if (ok) {
            result = "{\"ok\":true,\"mode\":\"reload\",\"message\":\"engine reloaded\"}";
        }
        else {
            std::ifstream log("error_log.log");
            std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
            if (text.size() > 3000) text.resize(3000);
            if (text.empty()) text = "script compiled but did not produce an engine";
            result = "{\"ok\":false,\"mode\":\"reload\",\"error\":\"" + jsonEscape(text) + "\"}";
        }
    }

    // The file was written by us: do not let the file watcher process it a second time
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(liveEngineFile().c_str(), ec);
    if (!ec) m_liveStamp = stamp;
    m_livePending = false;

    return result;
}

// All requests of the engine builder, executed on the main thread
std::string EngineSimApplication::handleLiveRoute(
    const std::string &method,
    const std::string &path,
    const std::string &body)
{
    if (method == "POST" && path == "/engine") {
        return handleLiveRequest(body);
    }

    if (method == "GET" && path == "/dyno") {
        return m_dynoSweep.statusJson();
    }

    if (method == "POST" && path == "/dyno/stop") {
        m_dynoSweep.abort("stopped by user", m_iceEngine, m_simulator);
        return "{\"ok\":true}";
    }

    if (method == "POST" && path == "/dyno") {
        DynoSweep::Parameters params;
        std::string error;
        if (!DynoSweep::parseParameters(body, &params)) {
            return "{\"ok\":false,\"error\":\"invalid dyno parameters\"}";
        }

        if (!m_dynoSweep.start(params, m_iceEngine, m_simulator, &error)) {
            return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
        }

        if (m_infoCluster != nullptr) {
            m_infoCluster->setLogMessage("Dyno run started from the engine builder");
        }

        return "{\"ok\":true}";
    }

    return "{\"ok\":false,\"error\":\"unknown route\"}";
}

void EngineSimApplication::processEngineInput() {
    if (m_iceEngine == nullptr) {
        return;
    }

    const float dt = m_engine.GetFrameLength();
    const bool fineControlMode = m_engine.IsKeyDown(ysKey::Code::Space);

    const int mouseWheel = m_engine.GetMouseWheel();
    const int mouseWheelDelta = mouseWheel - m_lastMouseWheel;
    m_lastMouseWheel = mouseWheel;

    bool fineControlInUse = false;
    if (m_engine.IsKeyDown(ysKey::Code::Z)) {
        const double rate = fineControlMode
            ? 0.001
            : 0.01;

        Synthesizer::AudioParameters audioParams = m_simulator->synthesizer().getAudioParameters();
        audioParams.volume = clamp(audioParams.volume + mouseWheelDelta * rate * dt);

        m_simulator->synthesizer().setAudioParameters(audioParams);
        fineControlInUse = true;

        m_infoCluster->setLogMessage("[Z] - Set volume to " + std::to_string(audioParams.volume));
    }
    else if (m_engine.IsKeyDown(ysKey::Code::X)) {
        const double rate = fineControlMode
            ? 0.001
            : 0.01;

        Synthesizer::AudioParameters audioParams = m_simulator->synthesizer().getAudioParameters();
        audioParams.convolution = clamp(audioParams.convolution + mouseWheelDelta * rate * dt);

        m_simulator->synthesizer().setAudioParameters(audioParams);
        fineControlInUse = true;

        m_infoCluster->setLogMessage("[X] - Set convolution level to " + std::to_string(audioParams.convolution));
    }
    else if (m_engine.IsKeyDown(ysKey::Code::C)) {
        const double rate = fineControlMode
            ? 0.00001
            : 0.001;

        Synthesizer::AudioParameters audioParams = m_simulator->synthesizer().getAudioParameters();
        audioParams.dF_F_mix = clamp(audioParams.dF_F_mix + mouseWheelDelta * rate * dt);

        m_simulator->synthesizer().setAudioParameters(audioParams);
        fineControlInUse = true;

        m_infoCluster->setLogMessage("[C] - Set high freq. gain to " + std::to_string(audioParams.dF_F_mix));
    }
    else if (m_engine.IsKeyDown(ysKey::Code::V)) {
        const double rate = fineControlMode
            ? 0.001
            : 0.01;

        Synthesizer::AudioParameters audioParams = m_simulator->synthesizer().getAudioParameters();
        audioParams.airNoise = clamp(audioParams.airNoise + mouseWheelDelta * rate * dt);

        m_simulator->synthesizer().setAudioParameters(audioParams);
        fineControlInUse = true;

        m_infoCluster->setLogMessage("[V] - Set low freq. noise to " + std::to_string(audioParams.airNoise));
    }
    else if (m_engine.IsKeyDown(ysKey::Code::B)) {
        const double rate = fineControlMode
            ? 0.001
            : 0.01;

        Synthesizer::AudioParameters audioParams = m_simulator->synthesizer().getAudioParameters();
        audioParams.inputSampleNoise = clamp(audioParams.inputSampleNoise + mouseWheelDelta * rate * dt);

        m_simulator->synthesizer().setAudioParameters(audioParams);
        fineControlInUse = true;

        m_infoCluster->setLogMessage("[B] - Set high freq. noise to " + std::to_string(audioParams.inputSampleNoise));
    }
    else if (m_engine.IsKeyDown(ysKey::Code::N)) {
        const double rate = fineControlMode
            ? 10.0
            : 100.0;

        const double newSimulationFrequency = clamp(
            m_simulator->getSimulationFrequency() + mouseWheelDelta * rate * dt,
            400.0, 400000.0);

        m_simulator->setSimulationFrequency(newSimulationFrequency);
        fineControlInUse = true;

        m_infoCluster->setLogMessage("[N] - Set simulation freq to " + std::to_string(m_simulator->getSimulationFrequency()));
    }
    else if (m_engine.IsKeyDown(ysKey::Code::G) && m_simulator->m_dyno.m_hold) {
        if (mouseWheelDelta > 0) {
            m_dynoSpeed += m_iceEngine->getDynoHoldStep();
        }
        else if (mouseWheelDelta < 0) {
            m_dynoSpeed -= m_iceEngine->getDynoHoldStep();
        }

        m_dynoSpeed = clamp(m_dynoSpeed, m_iceEngine->getDynoMinSpeed(), m_iceEngine->getDynoMaxSpeed());

        m_infoCluster->setLogMessage("[G] - Set dyno speed to " + std::to_string(units::toRpm(m_dynoSpeed)));
        fineControlInUse = true;
    }

    const double prevTargetThrottle = m_targetSpeedSetting;
    m_targetSpeedSetting = fineControlMode ? m_targetSpeedSetting : 0.0;
    if (m_engine.IsKeyDown(ysKey::Code::Q)) {
        m_targetSpeedSetting = 0.01;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::W)) {
        m_targetSpeedSetting = 0.1;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::E)) {
        m_targetSpeedSetting = 0.2;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::R)) {
        m_targetSpeedSetting = 1.0;
    }
    else if (fineControlMode && !fineControlInUse) {
        m_targetSpeedSetting = clamp(m_targetSpeedSetting + mouseWheelDelta * 0.0001);
    }

    if (prevTargetThrottle != m_targetSpeedSetting) {
        m_infoCluster->setLogMessage("Speed control set to " + std::to_string(m_targetSpeedSetting));
    }

    m_speedSetting = m_targetSpeedSetting * 0.5 + 0.5 * m_speedSetting;

    m_iceEngine->setSpeedControl(m_speedSetting);
    if (m_engine.ProcessKeyDown(ysKey::Code::M)) {
        const int currentLayer = getViewParameters().Layer0;
        if (currentLayer + 1 < m_iceEngine->getMaxDepth()) {
            setViewLayer(currentLayer + 1);
        }

        m_infoCluster->setLogMessage("[M] - Set render layer to " + std::to_string(getViewParameters().Layer0));
    }

    if (m_engine.ProcessKeyDown(ysKey::Code::OEM_Comma)) {
        if (getViewParameters().Layer0 - 1 >= 0)
            setViewLayer(getViewParameters().Layer0 - 1);

        m_infoCluster->setLogMessage("[,] - Set render layer to " + std::to_string(getViewParameters().Layer0));
    }

    if (m_engine.ProcessKeyDown(ysKey::Code::D)) {
        m_simulator->m_dyno.m_enabled = !m_simulator->m_dyno.m_enabled;

        const std::string msg = m_simulator->m_dyno.m_enabled
            ? "DYNOMOMETER ENABLED"
            : "DYNOMOMETER DISABLED";
        m_infoCluster->setLogMessage(msg);
    }

    if (m_engine.ProcessKeyDown(ysKey::Code::H)) {
        m_simulator->m_dyno.m_hold = !m_simulator->m_dyno.m_hold;

        const std::string msg = m_simulator->m_dyno.m_hold
            ? m_simulator->m_dyno.m_enabled ? "HOLD ENABLED" : "HOLD ON STANDBY [ENABLE DYNO. FOR HOLD]"
            : "HOLD DISABLED";
        m_infoCluster->setLogMessage(msg);
    }

    if (m_simulator->m_dyno.m_enabled) {
        if (!m_simulator->m_dyno.m_hold) {
            if (m_simulator->getFilteredDynoTorque() > units::torque(1.0, units::ft_lb)) {
                m_dynoSpeed += units::rpm(500) * dt;
            }
            else {
                m_dynoSpeed *= (1 / (1 + dt));
            }

            if (m_dynoSpeed > m_iceEngine->getRedline()) {
                m_simulator->m_dyno.m_enabled = false;
                m_dynoSpeed = units::rpm(0);
            }
        }
    }
    else {
        if (!m_simulator->m_dyno.m_hold) {
            m_dynoSpeed = units::rpm(0);
        }
    }

    m_dynoSpeed = clamp(m_dynoSpeed, m_iceEngine->getDynoMinSpeed(), m_iceEngine->getDynoMaxSpeed());
    m_simulator->m_dyno.m_rotationSpeed = m_dynoSpeed;

    const bool prevStarterEnabled = m_simulator->m_starterMotor.m_enabled;
    if (m_engine.IsKeyDown(ysKey::Code::S)) {
        m_simulator->m_starterMotor.m_enabled = true;
    }
    else {
        m_simulator->m_starterMotor.m_enabled = false;
    }

    if (prevStarterEnabled != m_simulator->m_starterMotor.m_enabled) {
        const std::string msg = m_simulator->m_starterMotor.m_enabled
            ? "STARTER ENABLED"
            : "STARTER DISABLED";
        m_infoCluster->setLogMessage(msg);
    }

    if (m_engine.ProcessKeyDown(ysKey::Code::A)) {
        m_simulator->getEngine()->getIgnitionModule()->m_enabled =
            !m_simulator->getEngine()->getIgnitionModule()->m_enabled;

        const std::string msg = m_simulator->getEngine()->getIgnitionModule()->m_enabled
            ? "IGNITION ENABLED"
            : "IGNITION DISABLED";
        m_infoCluster->setLogMessage(msg);
    }

    if (m_engine.ProcessKeyDown(ysKey::Code::Up)) {
        m_simulator->getTransmission()->changeGear(m_simulator->getTransmission()->getGear() + 1);

        m_infoCluster->setLogMessage(
            "UPSHIFTED TO " + std::to_string(m_simulator->getTransmission()->getGear() + 1));
    }
    else if (m_engine.ProcessKeyDown(ysKey::Code::Down)) {
        m_simulator->getTransmission()->changeGear(m_simulator->getTransmission()->getGear() - 1);

        if (m_simulator->getTransmission()->getGear() != -1) {
            m_infoCluster->setLogMessage(
                "DOWNSHIFTED TO " + std::to_string(m_simulator->getTransmission()->getGear() + 1));
        }
        else {
            m_infoCluster->setLogMessage("SHIFTED TO NEUTRAL");
        }
    }

    if (m_engine.IsKeyDown(ysKey::Code::T)) {
        m_targetClutchPressure -= 0.2 * dt;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::U)) {
        m_targetClutchPressure += 0.2 * dt;
    }
    else if (m_engine.IsKeyDown(ysKey::Code::Shift)) {
        m_targetClutchPressure = 0.0;
        m_infoCluster->setLogMessage("CLUTCH DEPRESSED");
    }
    else if (!m_engine.IsKeyDown(ysKey::Code::Y)) {
        m_targetClutchPressure = 1.0;
    }

    m_targetClutchPressure = clamp(m_targetClutchPressure);

    double clutchRC = 0.001;
    if (m_engine.IsKeyDown(ysKey::Code::Space)) {
        clutchRC = 1.0;
    }

    const double clutch_s = dt / (dt + clutchRC);
    m_clutchPressure = m_clutchPressure * (1 - clutch_s) + m_targetClutchPressure * clutch_s;
    m_simulator->getTransmission()->setClutchPressure(m_clutchPressure);
}

void EngineSimApplication::renderScene() {
    getShaders()->ResetBaseColor();
    getShaders()->SetObjectTransform(ysMath::LoadIdentity());

    m_textRenderer.SetColor(ysColor::linearToSrgb(m_foreground));
    m_shaders.SetClearColor(ysColor::linearToSrgb(m_shadow));

    const int screenWidth = m_engine.GetGameWindow()->GetGameWidth();
    const int screenHeight = m_engine.GetGameWindow()->GetGameHeight();
    const float aspectRatio = screenWidth / (float)screenHeight;

    const Point cameraPos = m_engineView->getCameraPosition();
    m_shaders.m_cameraPosition = ysMath::LoadVector(cameraPos.x, cameraPos.y);

    m_shaders.CalculateUiCamera(screenWidth, screenHeight);

    if (m_screen == 0) {
        Bounds windowBounds((float)screenWidth, (float)screenHeight, { 0, (float)screenHeight });
        Grid grid;
        grid.v_cells = 2;
        grid.h_cells = 3;
        Grid grid3x3;
        grid3x3.v_cells = 3;
        grid3x3.h_cells = 3;
        m_engineView->setDrawFrame(true);
        m_engineView->setBounds(grid.get(windowBounds, 1, 0, 1, 1));
        m_engineView->setLocalPosition({ 0, 0 });

        m_rightGaugeCluster->m_bounds = grid.get(windowBounds, 2, 0, 1, 2);
        m_oscCluster->m_bounds = grid.get(windowBounds, 1, 1);
        m_performanceCluster->m_bounds = grid3x3.get(windowBounds, 0, 1);
        m_tuningCluster->m_bounds = grid3x3.get(windowBounds, 0, 1);
        m_loadSimulationCluster->m_bounds = grid3x3.get(windowBounds, 0, 2);

        Grid grid1x3;
        grid1x3.v_cells = 3;
        grid1x3.h_cells = 1;
        m_mixerCluster->m_bounds = grid1x3.get(grid3x3.get(windowBounds, 0, 0), 0, 2);
        m_infoCluster->m_bounds = grid3x3.get(windowBounds, 0, 0);

        m_engineView->setVisible(true);
        m_rightGaugeCluster->setVisible(true);
        m_oscCluster->setVisible(true);
        m_performanceCluster->setVisible(false);
        m_tuningCluster->setVisible(true);
        m_loadSimulationCluster->setVisible(true);
        m_mixerCluster->setVisible(false);
        m_infoCluster->setVisible(true);

        m_oscCluster->activate();
    }
    else if (m_screen == 1) {
        Bounds windowBounds((float)screenWidth, (float)screenHeight, { 0, (float)screenHeight });
        m_engineView->setDrawFrame(false);
        m_engineView->setBounds(windowBounds);
        m_engineView->setLocalPosition({ 0, 0 });
        m_engineView->activate();

        m_engineView->setVisible(true);
        m_rightGaugeCluster->setVisible(false);
        m_oscCluster->setVisible(false);
        m_performanceCluster->setVisible(false);
        m_tuningCluster->setVisible(false);
        m_loadSimulationCluster->setVisible(false);
        m_mixerCluster->setVisible(false);
        m_infoCluster->setVisible(false);
    }
    else if (m_screen == 2) {
        Bounds windowBounds((float)screenWidth, (float)screenHeight, { 0, (float)screenHeight });
        Grid grid;
        grid.v_cells = 1;
        grid.h_cells = 3;
        m_engineView->setDrawFrame(true);
        m_engineView->setBounds(grid.get(windowBounds, 0, 0, 2, 1));
        m_engineView->setLocalPosition({ 0, 0 });
        m_engineView->activate();

        m_rightGaugeCluster->m_bounds = grid.get(windowBounds, 2, 0, 1, 1);

        m_engineView->setVisible(true);
        m_rightGaugeCluster->setVisible(true);
        m_oscCluster->setVisible(false);
        m_performanceCluster->setVisible(false);
        m_tuningCluster->setVisible(false);
        m_loadSimulationCluster->setVisible(false);
        m_mixerCluster->setVisible(false);
        m_infoCluster->setVisible(false);
    }

    const float cameraAspectRatio =
        m_engineView->m_bounds.width() / m_engineView->m_bounds.height();
    m_engine.GetDevice()->ResizeRenderTarget(
        m_mainRenderTarget,
        m_engineView->m_bounds.width(),
        m_engineView->m_bounds.height(),
        m_engineView->m_bounds.width(),
        m_engineView->m_bounds.height()
    );
    m_engine.GetDevice()->RepositionRenderTarget(
        m_mainRenderTarget,
        m_engineView->m_bounds.getPosition(Bounds::tl).x,
        screenHeight - m_engineView->m_bounds.getPosition(Bounds::tl).y
    );
    m_shaders.CalculateCamera(
        cameraAspectRatio * m_displayHeight / m_engineView->m_zoom,
        m_displayHeight / m_engineView->m_zoom,
        m_engineView->m_bounds,
        m_screenWidth,
        m_screenHeight,
        m_displayAngle);

    m_geometryGenerator.reset();

    render();

    m_engine.GetDevice()->EditBufferDataRange(
        m_geometryVertexBuffer,
        (char *)m_geometryGenerator.getVertexData(),
        sizeof(dbasic::Vertex) * m_geometryGenerator.getCurrentVertexCount(),
        0);

    m_engine.GetDevice()->EditBufferDataRange(
        m_geometryIndexBuffer,
        (char *)m_geometryGenerator.getIndexData(),
        sizeof(unsigned short) * m_geometryGenerator.getCurrentIndexCount(),
        0);
}

void EngineSimApplication::refreshUserInterface() {
    m_uiManager.destroy();
    m_uiManager.initialize(this);

    m_engineView = m_uiManager.getRoot()->addElement<EngineView>();
    m_rightGaugeCluster = m_uiManager.getRoot()->addElement<RightGaugeCluster>();
    m_oscCluster = m_uiManager.getRoot()->addElement<OscilloscopeCluster>();
    m_performanceCluster = m_uiManager.getRoot()->addElement<PerformanceCluster>();
    m_tuningCluster = m_uiManager.getRoot()->addElement<TuningCluster>();
    m_loadSimulationCluster = m_uiManager.getRoot()->addElement<LoadSimulationCluster>();
    m_mixerCluster = m_uiManager.getRoot()->addElement<MixerCluster>();
    m_infoCluster = m_uiManager.getRoot()->addElement<InfoCluster>();

    m_infoCluster->setEngine(m_iceEngine);
    m_rightGaugeCluster->m_simulator = m_simulator;
    m_rightGaugeCluster->setEngine(m_iceEngine);
    m_oscCluster->setSimulator(m_simulator);
    if (m_iceEngine != nullptr) {
        m_oscCluster->setDynoMaxRange(units::toRpm(m_iceEngine->getRedline()));
    }
    m_performanceCluster->setSimulator(m_simulator);
    m_tuningCluster->setEngine(m_iceEngine);
    m_loadSimulationCluster->setSimulator(m_simulator);
    m_mixerCluster->setSimulator(m_simulator);
}

void EngineSimApplication::startRecording() {
    m_recording = true;

#ifdef ATG_ENGINE_SIM_VIDEO_CAPTURE
    atg_dtv::Encoder::VideoSettings settings{};

    // Output filename
    settings.fname = "../workspace/video_capture/engine_sim_video_capture.mp4";
    settings.inputWidth = m_engine.GetScreenWidth();
    settings.inputHeight = m_engine.GetScreenHeight();
    settings.width = settings.inputWidth;
    settings.height = settings.inputHeight;
    settings.hardwareEncoding = true;
    settings.inputAlpha = true;
    settings.bitRate = 40000000;

    m_encoder.run(settings, 2);
#endif /* ATG_ENGINE_SIM_VIDEO_CAPTURE */
}

void EngineSimApplication::updateScreenSizeStability() {
    m_screenResolution[m_screenResolutionIndex][0] = m_engine.GetScreenWidth();
    m_screenResolution[m_screenResolutionIndex][1] = m_engine.GetScreenHeight();

    m_screenResolutionIndex = (m_screenResolutionIndex + 1) % ScreenResolutionHistoryLength;
}

bool EngineSimApplication::readyToRecord() {
    const int w = m_screenResolution[0][0];
    const int h = m_screenResolution[0][1];

    if (w <= 0 && h <= 0) return false;
    if ((w % 2) != 0 || (h % 2) != 0) return false;

    for (int i = 1; i < ScreenResolutionHistoryLength; ++i) {
        if (m_screenResolution[i][0] != w) return false;
        if (m_screenResolution[i][1] != h) return false;
    }

    return true;
}

void EngineSimApplication::stopRecording() {
    m_recording = false;

#ifdef ATG_ENGINE_SIM_VIDEO_CAPTURE
    m_encoder.commit();
    m_encoder.stop();
#endif /* ATG_ENGINE_SIM_VIDEO_CAPTURE */
}

void EngineSimApplication::recordFrame() {
#ifdef ATG_ENGINE_SIM_VIDEO_CAPTURE
    atg_dtv::Frame *frame = m_encoder.newFrame(false);
    if (frame != nullptr && m_encoder.getError() == atg_dtv::Encoder::Error::None) {
        m_engine.GetDevice()->ReadRenderTarget(m_engine.GetScreenRenderTarget(), frame->m_rgb);
    }

    m_encoder.submitFrame();
#endif /* ATG_ENGINE_SIM_VIDEO_CAPTURE */
}
