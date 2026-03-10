#include <iostream>
#include <iomanip>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <regex>
#include <random>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include "libs/glad/glad.h"					 // library for OpenGL functions loading (like glClear or glViewport)
#include "libs/glm/glm.hpp"					 // library for OpenGL style mathematics (basic vector and matrix mathematics functions)
#include "libs/glm/gtc/matrix_transform.hpp" // for matrix transformation functions
#include "libs/glm/gtc/type_ptr.hpp"		 // for matrix conversion to raw pointers (OpenGL compatibility with GLM)
#include "libs/stb_image.h"					 // library for image loading
#include "libs/imgui/imgui.h"				 // library for UI elements
#include "libs/imgui/imgui_impl_opengl3.h"	 // connects Dear ImGui with OpenGL
#include "libs/imgui/imgui_impl_glfw.h"		 // connects Dear ImGui with GLFW
#include "libs/imgui/ImGuiFileDialog.h"		 // extension for file browsing dialog

#include "shader.h"	 // implementation of the graphics pipeline
#include "camera.h"	 // implementation of the camera system
#include "sound.h"	 // implementation of the sound playback
#include "light.h"	 // definition of the light settings and light cube
#include "model.h"	 // BDAE Model: class for parsing .bdae file and rendering 3D model
#include "terrain.h" // Game Engine: class for parsing .trn, .itm, .phy, .nav, .msk, and .shw files, and rendering 3D map
#include "parserTRN.h"

#ifdef __linux__
#include <GLFW/glfw3.h> // library for creating windows and handling input – mouse clicks, keyboard input, or window resizes
#elif _WIN32
#include "libs/glfw/glfw3.h"
#endif

void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);
void processInput(GLFWwindow *window);

// window settings
bool isFullscreen = false;
const unsigned int DEFAULT_WINDOW_WIDTH = 800;
const unsigned int DEFAULT_WINDOW_HEIGHT = 600;
const unsigned int DEFAULT_WINDOW_POS_X = 100;
const unsigned int DEFAULT_WINDOW_POS_Y = 100;

unsigned int currentWindowWidth = DEFAULT_WINDOW_WIDTH;
unsigned int currentWindowHeight = DEFAULT_WINDOW_HEIGHT;

// create a Camera class instance with a specified position and default values for other parameters, to access its functionality
Camera ourCamera;

// for passing local application variables to callback functions (GLFW supports only one user pointer per window, so to pass multiple objects, we wrap them in a context struct and pass a pointer to that struct)
struct AppContext
{
	Model *model;
	Light *light;
	Terrain *terrain;
};

float deltaTime = 0.0f; // time between current frame and last frame
float lastFrame = 0.0f; // time of last frame

bool firstMouse = true;						// flag to check if the mouse movement is being processed for the first time
double lastX = DEFAULT_WINDOW_WIDTH / 2.0;	// starting cursor position (x-axis)
double lastY = DEFAULT_WINDOW_HEIGHT / 2.0; // starting cursor position (y-axis)
bool rmbLookCaptureActive = false;

// viewer variables
bool fileDialogOpen = false;	   // flag that indicates whether to block all background inputs (when the file browsing dialog is open)
bool settingsPanelHovered = false; // flag that indicated whether to block background mouse input (when interacting with the settings panel)
bool displayBaseMesh = false;	   // flag that indicates base / textured mesh display mode
bool displayNavMesh = false;	   // flag that indicates whether to show walkable surfaces
bool displayPhysics = false;	   // flag that indicates whether to show physical surfaces
bool isTerrainViewer = false;
bool effectPreviewEnabled = false; // toggles simple in-viewer particle preview
bool effectPreviewBoost = true;	// force high-visibility debug effect rendering
std::vector<std::string> armorModelPaths;
std::unordered_map<std::string, int> armorIndexByPath;
int currentArmorModelIndex = -1;
Sound *shortcutSound = nullptr;

constexpr int HOTBAR_SLOT_COUNT = 12;
bool hotbarEnabled = true;
std::vector<unsigned int> hotbarIconTextures;
std::vector<std::string> hotbarIconNames;
std::array<float, HOTBAR_SLOT_COUNT> hotbarCooldownStartTimes = {};
std::array<float, HOTBAR_SLOT_COUNT> hotbarCooldownDurations = {};
std::string hotbarClickSfxPath;
std::string hotbarUnavailableSfxPath;

struct RacePreset
{
	std::string label;
	std::string modelPath;
};

std::vector<std::string> terrainMapPaths;
std::vector<RacePreset> racePresets;
int selectedRacePreset = 0;
bool characterControlEnabled = true;
bool cameraFollowEnabled = true;
bool gameModeEnabled = false;
glm::vec3 playerWorldPos(0.0f, 0.0f, 0.0f);
float playerYawDegrees = 0.0f;
float playerWalkSpeed = 7.5f;
float playerRunSpeed = 15.0f;
float cameraFollowDistance = 32.0f;
float cameraFollowHeight = 6.0f;
float playerModelScale = 6.0f;
float playerModelYOffset = 0.0f;
float playerFacingYawOffset = 0.0f;
float thirdPersonYaw = -90.0f;
float thirdPersonPitch = -12.0f;
float playerVerticalVelocity = 0.0f;
bool playerOnGround = true;
float playerGravity = 36.0f;
float playerJumpVelocity = 13.5f;
int playerIdleAnimIndex = -1;
int playerWalkAnimIndex = -1;
int playerRunAnimIndex = -1;
bool autoPlayerScaleEnabled = true;
float autoScaleLastNativeHeight = 0.0f;
float autoScaleLastReferenceHeight = 0.0f;
int autoScaleLastSampleCount = 0;

static std::string normalizeModelPathKey(const std::string &path)
{
	std::error_code ec;
	std::filesystem::path abs = std::filesystem::absolute(path, ec);
	std::string s = ec ? path : abs.string();
	for (char &c : s)
	{
		if (c == '\\')
			c = '/';
		else
			c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

static void buildArmorModelList()
{
	armorModelPaths.clear();
	armorIndexByPath.clear();
	const std::vector<std::string> roots = {
		"oac1osp/data/model/item/armor",
		"data/model/item/armor",
	};
	std::unordered_set<std::string> seen;
	for (const std::string &root : roots)
	{
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
			continue;
		for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(root, ec))
		{
			if (ec || !entry.is_regular_file())
				continue;
			if (entry.path().extension() != ".bdae")
				continue;
			std::string key = normalizeModelPathKey(entry.path().string());
			if (seen.insert(key).second)
				armorModelPaths.push_back(entry.path().string());
		}
	}
	std::sort(armorModelPaths.begin(), armorModelPaths.end());
	for (int i = 0; i < (int)armorModelPaths.size(); i++)
		armorIndexByPath[normalizeModelPathKey(armorModelPaths[i])] = i;
}

static void syncArmorModelIndex(const std::string &loadedPath)
{
	std::string key = normalizeModelPathKey(loadedPath);
	auto it = armorIndexByPath.find(key);
	currentArmorModelIndex = (it != armorIndexByPath.end()) ? it->second : -1;
}

static void buildTerrainMapList()
{
	terrainMapPaths.clear();
	std::unordered_set<std::string> seen;
	const std::vector<std::string> roots = {
		"oac1osp/data/terrain",
		"data/terrain",
	};
	for (const std::string &root : roots)
	{
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
			continue;
		for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(root, ec))
		{
			if (ec || !entry.is_regular_file() || entry.path().extension() != ".trn")
				continue;
			std::string key = normalizeModelPathKey(entry.path().string());
			if (seen.insert(key).second)
				terrainMapPaths.push_back(entry.path().string());
		}
	}
	std::sort(terrainMapPaths.begin(), terrainMapPaths.end());
}

static void buildRacePresetList()
{
	racePresets.clear();

	const std::vector<RacePreset> candidates = {
		{"Human Male", "oac1osp/data/model/npc/character/human/male/human_m_h.bdae"},
		{"Human Female", "oac1osp/data/model/npc/character/human/female/human_f_h.bdae"},
		{"Orc Male", "oac1osp/data/model/npc/character/orc/male/orc_m_h.bdae"},
		{"Elf Male", "oac1osp/data/model/npc/character/elf/male/elf_m_h.bdae"},
		{"Undead Male", "oac1osp/data/model/npc/character/undead/male/undead_m_h.bdae"},
	};

	for (const RacePreset &preset : candidates)
	{
		std::error_code ec;
		if (std::filesystem::exists(preset.modelPath, ec))
			racePresets.push_back(preset);
	}

	if (racePresets.empty())
	{
		racePresets.push_back({"Human Male", "oac1osp/data/model/npc/character/human/male/human_m_l.bdae"});
	}
	if (selectedRacePreset >= (int)racePresets.size())
		selectedRacePreset = 0;
}

struct ParsedBeff
{
	int version = 0;
	unsigned int sectionCount = 0;
	unsigned int emitterCount = 0;
	unsigned int blendHint = 0;
	unsigned int optionA = 0;
	unsigned int optionB = 0;
	std::vector<std::string> textureRefs;
	float particleLife = 0.6f;
	float particleSpeed = 0.8f;
	float particleSpread = 0.18f;
	float particleSize = 8.0f;
	float alphaStart = 1.0f;
	float alphaEnd = 0.15f;
	int spawnPerFrame = 3;
	int inferredRecordCount = 0;
};

static ParsedBeff parseBeffFile(const std::string &path)
{
	ParsedBeff out;
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return out;

	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (bytes.size() < 8)
		return out;

	if (!(bytes[0] == 'b' && bytes[1] == 'e' && bytes[2] == 'f' && bytes[3] == 'f'))
		return out;

	memcpy(&out.version, bytes.data() + 4, sizeof(int));
	if (bytes.size() >= 64)
	{
		memcpy(&out.sectionCount, bytes.data() + 16, sizeof(unsigned int));
		memcpy(&out.optionA, bytes.data() + 32, sizeof(unsigned int));
		memcpy(&out.emitterCount, bytes.data() + 40, sizeof(unsigned int));
		memcpy(&out.blendHint, bytes.data() + 48, sizeof(unsigned int));
		memcpy(&out.optionB, bytes.data() + 56, sizeof(unsigned int));
	}

	std::unordered_set<std::string> seen;
	std::string current;
	for (size_t i = 0; i < bytes.size(); i++)
	{
		unsigned char c = bytes[i];
		if (c >= 32 && c <= 126)
		{
			current.push_back((char)c);
			continue;
		}

		if (current.size() >= 5)
		{
			std::string lower = current;
			for (char &ch : lower)
				ch = (char)std::tolower((unsigned char)ch);

			if ((lower.find(".tga") != std::string::npos || lower.find(".png") != std::string::npos || lower.find(".dds") != std::string::npos) &&
				seen.find(lower) == seen.end())
			{
				seen.insert(lower);
				out.textureRefs.push_back(current);
			}
		}
		current.clear();
	}

	// Parse common parameter candidates.
	// Prefer template-derived BEFF structure fields; keep raw record scan as fallback.
	std::vector<float> sizeCandidates;
	std::vector<float> sizeEndCandidates;
	std::vector<float> lifeCandidates;
	std::vector<float> speedCandidates;
	std::vector<float> spreadCandidates;
	std::vector<float> alphaStartCandidates;
	std::vector<float> alphaEndCandidates;
	std::vector<int> rateCandidates;

	auto readI32 = [&](size_t &off, int &v) -> bool
	{
		if (off + 4 > bytes.size())
			return false;
		std::memcpy(&v, bytes.data() + off, 4);
		off += 4;
		return true;
	};
	auto readU32 = [&](size_t &off, uint32_t &v) -> bool
	{
		if (off + 4 > bytes.size())
			return false;
		std::memcpy(&v, bytes.data() + off, 4);
		off += 4;
		return true;
	};
	auto readU16 = [&](size_t &off, uint16_t &v) -> bool
	{
		if (off + 2 > bytes.size())
			return false;
		std::memcpy(&v, bytes.data() + off, 2);
		off += 2;
		return true;
	};
	auto readF32 = [&](size_t &off, float &v) -> bool
	{
		if (off + 4 > bytes.size())
			return false;
		std::memcpy(&v, bytes.data() + off, 4);
		off += 4;
		return true;
	};
	auto parseAlignedString = [&](size_t &off, std::string *textOut = nullptr) -> bool
	{
		int len = 0;
		if (!readI32(off, len))
			return false;
		if (len < 0 || off + (size_t)len > bytes.size())
			return false;
		if (len > 0)
		{
			std::string s((const char *)bytes.data() + off, (size_t)len);
			size_t nul = s.find('\0');
			if (nul != std::string::npos)
				s.resize(nul);
			if (textOut)
				*textOut = s;

			std::string lower = s;
			for (char &ch : lower)
				ch = (char)std::tolower((unsigned char)ch);
			if ((lower.find(".tga") != std::string::npos || lower.find(".png") != std::string::npos || lower.find(".dds") != std::string::npos) &&
				seen.find(lower) == seen.end())
			{
				seen.insert(lower);
				out.textureRefs.push_back(s);
			}
		}
		off += (size_t)len;
		return true;
	};
	auto parseCKeyData = [&](size_t &off, char kind, float *firstFloat, float *lastFloat, int *firstInt, int *lastInt) -> bool
	{
		uint32_t keyCount = 0;
		int isLoop = 0;
		if (!readU32(off, keyCount) || !readI32(off, isLoop))
			return false;
		for (uint32_t i = 0; i < keyCount; i++)
		{
			int frame = 0;
			if (!readI32(off, frame))
				return false;
			if (kind == 'i')
			{
				int v = 0;
				if (!readI32(off, v))
					return false;
				if (i == 0 && firstInt)
					*firstInt = v;
				if (i + 1 == keyCount && lastInt)
					*lastInt = v;
			}
			else if (kind == 'f')
			{
				float v = 0.0f;
				if (!readF32(off, v))
					return false;
				if (i == 0 && firstFloat)
					*firstFloat = v;
				if (i + 1 == keyCount && lastFloat)
					*lastFloat = v;
			}
			else if (kind == '3')
			{
				float x = 0.0f, y = 0.0f, z = 0.0f;
				if (!readF32(off, x) || !readF32(off, y) || !readF32(off, z))
					return false;
				(void)x;
				(void)y;
				(void)z;
			}
		}
		return true;
	};
	std::function<bool(size_t &, int)> parseAffectors = [&](size_t &off, int depth) -> bool
	{
		(void)depth;
		int count = 0;
		if (!readI32(off, count))
			return false;
		if (count < 0 || count > 128)
			return false;
		for (int i = 0; i < count; i++)
		{
			int id = 0;
			int type = 0;
			if (!readI32(off, id) || !readI32(off, type))
				return false;
			(void)id;
			if (type == 0)
			{
				if (!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, '3', nullptr, nullptr, nullptr, nullptr))
					return false;
			}
			else if (type == 1)
			{
				if (!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr))
					return false;
			}
			else if (type == 2)
			{
				if (!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||
					!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr))
					return false;
			}
			else if (type == 3)
			{
				int affectType = 0;
				if (!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, '3', nullptr, nullptr, nullptr, nullptr) || !readI32(off, affectType))
					return false;
			}
			else
			{
				return false;
			}
		}
		return true;
	};
	std::function<bool(size_t &, int)> parseEmitter = [&](size_t &off, int depth) -> bool
	{
		if (depth > 16)
			return false;
		std::string parentNode;
		if (!parseAlignedString(off, &parentNode))
			return false;

		int subEmitters = 0;
		if (!readI32(off, subEmitters))
			return false;
		if (subEmitters < 0 || subEmitters > 64)
			return false;

		int emitterType = 0;
		uint16_t music = 0;
		uint16_t billboard = 0;
		int follow = 0, emitVolRand = 0, emitTime = 0, emitStart = 0, interval = 0, intervalRand = 0;
		int dieWithEmitter = 0, towardTarget = 0, speedAllRand = 0, speedSpeRand = 0;
		if (!readI32(off, emitterType) || !readU16(off, music) || !readU16(off, billboard) ||
			!readI32(off, follow) || !readI32(off, emitVolRand) || !readI32(off, emitTime) || !readI32(off, emitStart) ||
			!readI32(off, interval) || !readI32(off, intervalRand) || !readI32(off, dieWithEmitter) || !readI32(off, towardTarget) ||
			!readI32(off, speedAllRand) || !readI32(off, speedSpeRand))
			return false;
		(void)emitterType;
		(void)music;
		(void)billboard;
		(void)follow;
		(void)emitVolRand;
		(void)emitTime;
		(void)emitStart;
		(void)dieWithEmitter;
		(void)towardTarget;
		(void)speedAllRand;
		(void)speedSpeRand;

		float keySpeedAll = 0.0f;
		float keySpeedSpe = 0.0f;
		float keySpeedSpeAngle = 0.0f;
		if (!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // outer_x
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // outer_y
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // outer_z
			!parseCKeyData(off, 'i', nullptr, nullptr, nullptr, nullptr) ||			  // inner
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // rotate_x
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // rotate_y
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // rotate_z
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // translate_x
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // translate_y
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // translate_z
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||			  // emit_volumn
			!parseCKeyData(off, 'f', &keySpeedAll, nullptr, nullptr, nullptr) ||		  // speed_all
			!parseCKeyData(off, 'f', &keySpeedSpe, nullptr, nullptr, nullptr) ||		  // speed_spe
			!parseCKeyData(off, 'f', &keySpeedSpeAngle, nullptr, nullptr, nullptr) || // speed_spe_angle
			!parseCKeyData(off, '3', nullptr, nullptr, nullptr, nullptr))			  // spe_dir
			return false;

		// ParticleData
		int particleType = 0, particleFollow = 0, lifeTime = 0, lifeRandom = 0, sizeRandom = 0, keepXY = 0;
		float baseAngleX = 0.0f, baseAngleY = 0.0f, baseAngleZ = 0.0f;
		int baseAngleXRand = 0, baseAngleYRand = 0, baseAngleZRand = 0;
		int rotXRand = 0, rotYRand = 0, rotZRand = 0, scaleRand = 0, mrtlType = 0, pivotX = 0, pivotY = 0, uvRot = 0;
		uint16_t flipH = 0, flipV = 0;
		if (!readI32(off, particleType) || !readI32(off, particleFollow) || !readI32(off, lifeTime) || !readI32(off, lifeRandom) ||
			!readI32(off, sizeRandom) || !readI32(off, keepXY) || !readF32(off, baseAngleX) || !readI32(off, baseAngleXRand) ||
			!readF32(off, baseAngleY) || !readI32(off, baseAngleYRand) || !readF32(off, baseAngleZ) || !readI32(off, baseAngleZRand) ||
			!readI32(off, rotXRand) || !readI32(off, rotYRand) || !readI32(off, rotZRand) || !readI32(off, scaleRand) ||
			!readI32(off, mrtlType) || !readI32(off, pivotX) || !readI32(off, pivotY) || !readI32(off, uvRot) ||
			!readU16(off, flipH) || !readU16(off, flipV))
			return false;
		(void)particleType;
		(void)particleFollow;
		(void)sizeRandom;
		(void)keepXY;
		(void)baseAngleX;
		(void)baseAngleY;
		(void)baseAngleZ;
		(void)baseAngleXRand;
		(void)baseAngleYRand;
		(void)baseAngleZRand;
		(void)rotXRand;
		(void)rotYRand;
		(void)rotZRand;
		(void)scaleRand;
		(void)mrtlType;
		(void)pivotX;
		(void)pivotY;
		(void)uvRot;
		(void)flipH;
		(void)flipV;

		std::string textureName;
		if (!parseAlignedString(off, &textureName))
			return false;

		int alphaFirst = 255;
		int alphaLast = 255;
		if (!parseCKeyData(off, 'i', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, 'i', nullptr, nullptr, nullptr, nullptr) ||
			!parseCKeyData(off, 'i', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, 'i', nullptr, nullptr, &alphaFirst, &alphaLast))
			return false; // color rgba

		float baseSizeX = 0.0f;
		float baseSizeXEnd = 0.0f;
		float baseSizeY = 0.0f;
		float baseSizeYEnd = 0.0f;
		if (!parseCKeyData(off, 'f', &baseSizeX, &baseSizeXEnd, nullptr, nullptr) || !parseCKeyData(off, 'f', &baseSizeY, &baseSizeYEnd, nullptr, nullptr))
			return false;
		if (!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) || !parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr) ||
			!parseCKeyData(off, 'f', nullptr, nullptr, nullptr, nullptr))
			return false; // rotation xyz + scale xy

		if (!parseAffectors(off, depth))
			return false;

		// Convert parsed BEFF values into preview candidates.
		if (baseSizeX > 0.001f)
			sizeCandidates.push_back(baseSizeX);
		if (baseSizeY > 0.001f)
			sizeCandidates.push_back(baseSizeY);
		if (baseSizeXEnd > 0.001f)
			sizeEndCandidates.push_back(baseSizeXEnd);
		if (baseSizeYEnd > 0.001f)
			sizeEndCandidates.push_back(baseSizeYEnd);
		if (lifeTime > 0)
			lifeCandidates.push_back((float)lifeTime / 30.0f);
		if (lifeRandom > 0)
			lifeCandidates.push_back((float)lifeRandom / 30.0f);
		if (keySpeedAll > 0.001f)
			speedCandidates.push_back(keySpeedAll);
		if (keySpeedSpe > 0.001f)
			speedCandidates.push_back(keySpeedSpe);
		if (keySpeedSpeAngle > 0.001f)
		{
			float normalizedSpread = std::clamp(keySpeedSpeAngle / 6.28318f, 0.05f, 1.0f);
			spreadCandidates.push_back(normalizedSpread);
		}
		auto decodeAlphaKey = [](int v) -> float
		{
			// Some files appear to store alpha in 0..1 integer domain (0/1),
			// while others use 0..255.
			if (v >= 0 && v <= 1)
				return (float)v;
			return std::clamp((float)v / 255.0f, 0.0f, 1.0f);
		};
		alphaStartCandidates.push_back(decodeAlphaKey(alphaFirst));
		alphaEndCandidates.push_back(decodeAlphaKey(alphaLast));
		if (interval > 0)
			rateCandidates.push_back(std::max(1, 12 / interval));
		if (intervalRand > 0)
			rateCandidates.push_back(std::max(1, 12 / std::max(1, intervalRand)));

		for (int i = 0; i < subEmitters; i++)
			if (!parseEmitter(off, depth + 1))
				return false;
		return true;
	};

	// Template-driven parse pass (based on BEFF.bt / Emitter.bt / Particle.bt).
	{
		size_t off = 0;
		if (bytes.size() >= 20)
		{
			off = 4; // skip signature (already validated)
			uint32_t fmtVersion = 0;
			int delayMin = 0, delayMax = 0, nodesCount = 0;
			if (readU32(off, fmtVersion) && readI32(off, delayMin) && readI32(off, delayMax) && readI32(off, nodesCount) && nodesCount >= 0 && nodesCount <= 64)
			{
				out.version = (int)fmtVersion;
				out.sectionCount = (unsigned int)nodesCount;
				for (int n = 0; n < nodesCount; n++)
				{
					int nodeType = -1;
					if (!readI32(off, nodeType))
						break;
					if (nodeType == 0)
					{
						out.emitterCount++;
						if (!parseEmitter(off, 0))
							break;
					}
					else if (nodeType == 1 || nodeType == 2)
					{
						// Collada node/root
						if (!parseAlignedString(off, nullptr))
							break;
						int modelLoop = 0;
						if (!readI32(off, modelLoop) || !parseCKeyData(off, 'i', nullptr, nullptr, nullptr, nullptr))
							break;
					}
					else
					{
						break;
					}
				}
			}
		}
	}

	// Fallback/augmentation raw scan:
	// Parse common 16-byte parameter records [key:u32][0][0][value:u32/f32].
	for (size_t off = 64; off + 16 <= bytes.size(); off += 4)
	{
		uint32_t key = 0;
		uint32_t midA = 0;
		uint32_t midB = 0;
		uint32_t raw = 0;
		std::memcpy(&key, bytes.data() + off, sizeof(uint32_t));
		std::memcpy(&midA, bytes.data() + off + 4, sizeof(uint32_t));
		std::memcpy(&midB, bytes.data() + off + 8, sizeof(uint32_t));
		std::memcpy(&raw, bytes.data() + off + 12, sizeof(uint32_t));
		if (key == 0 || midA != 0 || midB != 0 || raw == 0)
			continue;

		out.inferredRecordCount++;

		float value = 0.0f;
		std::memcpy(&value, &raw, sizeof(float));
		if (std::isfinite(value) && value > 0.0f)
		{
			// Ignore likely angular constants for emitter cone/sweep params.
			if (std::abs(value - 6.28318f) < 0.02f)
				continue;

			if (value >= 0.05f && value <= 4.0f)
				sizeCandidates.push_back(value);
			if (value >= 0.05f && value <= 5.0f)
				lifeCandidates.push_back(value);
			if (value >= 0.02f && value <= 25.0f)
				speedCandidates.push_back(value);
			if (value >= 0.001f && value <= 1.2f)
				spreadCandidates.push_back(value);
			if (value >= 1.0f && value <= 120.0f)
				rateCandidates.push_back((int)std::lround(value));
		}

		if (raw >= 1 && raw <= 120)
			rateCandidates.push_back((int)raw);
	}

	auto pickMedianFloat = [](std::vector<float> values, float fallback) -> float
	{
		if (values.empty())
			return fallback;
		std::sort(values.begin(), values.end());
		return values[values.size() / 2];
	};
	auto pickMedianInt = [](std::vector<int> values, int fallback) -> int
	{
		if (values.empty())
			return fallback;
		std::sort(values.begin(), values.end());
		return values[values.size() / 2];
	};

	// Header-based fallback when we can't infer enough from record data.
	unsigned int sec = std::max(1u, out.sectionCount);
	unsigned int emit = std::max(1u, out.emitterCount);
	float fallbackLife = std::min(1.8f, 0.30f + 0.08f * (float)sec + 0.05f * (float)emit);
	float fallbackSpeed = std::min(2.2f, 0.35f + 0.06f * (float)(out.blendHint == 0 ? 1 : out.blendHint));
	float fallbackSpread = std::min(0.7f, 0.10f + 0.03f * (float)((out.optionA % 8) + 1));
	float fallbackSizePx = std::min(24.0f, 8.0f + 1.2f * (float)((out.optionB % 10) + 1));
	int fallbackSpawn = std::min(32, std::max(6, (int)emit * 4 + (int)sec));

	float inferredSizeNorm = pickMedianFloat(sizeCandidates, 0.7f);
	out.particleLife = std::clamp(pickMedianFloat(lifeCandidates, fallbackLife), 0.15f, 4.0f);
	out.particleSpeed = std::clamp(pickMedianFloat(speedCandidates, fallbackSpeed), 0.05f, 25.0f);
	out.particleSpread = std::clamp(pickMedianFloat(spreadCandidates, fallbackSpread), 0.01f, 1.2f);
	out.spawnPerFrame = std::clamp(pickMedianInt(rateCandidates, fallbackSpawn), 4, 64);
	out.particleSize = std::clamp(inferredSizeNorm * 18.0f, 8.0f, 36.0f);
	float inferredSizeEndNorm = pickMedianFloat(sizeEndCandidates, inferredSizeNorm * 0.55f);
	float alphaStart = pickMedianFloat(alphaStartCandidates, 1.0f);
	float alphaEnd = pickMedianFloat(alphaEndCandidates, 0.12f);
	out.alphaStart = std::clamp(alphaStart, 0.35f, 1.0f);
	out.alphaEnd = std::clamp(alphaEnd, 0.10f, out.alphaStart);
	// Keep debug preview visible even when BEFF alpha keys are sparse/zeroed.
	if (out.alphaStart <= 0.36f && out.alphaEnd <= 0.11f)
	{
		out.alphaStart = 0.85f;
		out.alphaEnd = 0.25f;
	}
	if (!sizeEndCandidates.empty())
		out.particleSize = std::clamp(((inferredSizeNorm + inferredSizeEndNorm) * 0.5f) * 18.0f, 8.0f, 36.0f);

	if (sizeCandidates.empty())
		out.particleSize = fallbackSizePx;

	return out;
}

static std::string toLowerAsciiString(const std::string &v)
{
	std::string out = v;
	for (char &ch : out)
		ch = (char)std::tolower((unsigned char)ch);
	return out;
}

static std::unordered_map<std::string, std::string> g_effectTextureByFile;
static std::unordered_map<std::string, std::string> g_effectTextureByStemPreferred;
static bool g_effectTextureIndexBuilt = false;

static void buildEffectTextureIndex()
{
	if (g_effectTextureIndexBuilt)
		return;
	g_effectTextureIndexBuilt = true;

	const std::vector<std::string> roots = {
		"oac1osp/data/texture",
		"oac1osp/data/texture2g",
		"oac1osp/data/texture_converted",
		"data/texture",
		"data/texture2g",
		"data/texture_converted",
	};
	for (const std::string &root : roots)
	{
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
			continue;
		for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(root, ec))
		{
			if (ec || !entry.is_regular_file())
				continue;
			std::string ext = toLowerAsciiString(entry.path().extension().string());
			if (ext != ".tga" && ext != ".png" && ext != ".bmp" && ext != ".jpg" && ext != ".jpeg")
				continue;
			std::string key = toLowerAsciiString(entry.path().filename().string());
			if (!key.empty() && g_effectTextureByFile.find(key) == g_effectTextureByFile.end())
				g_effectTextureByFile[key] = entry.path().string();

			std::string stem = toLowerAsciiString(entry.path().stem().string());
			if (!stem.empty())
			{
				auto itStem = g_effectTextureByStemPreferred.find(stem);
				bool prefer = (ext == ".png");
				if (itStem == g_effectTextureByStemPreferred.end())
					g_effectTextureByStemPreferred[stem] = entry.path().string();
				else if (prefer)
					g_effectTextureByStemPreferred[stem] = entry.path().string();
			}
		}
	}
}

static std::string resolveEffectTexturePath(const std::string &name)
{
	if (name.empty())
		return "";
	std::error_code ec;
	if (std::filesystem::exists(name, ec))
		return name;

	std::filesystem::path p(name);
	std::string fileOnly = toLowerAsciiString(p.filename().string());
	if (fileOnly.empty())
		return "";
	buildEffectTextureIndex();

	// Prefer converted PNG by stem, then fallback to exact filename match.
	std::string stem = toLowerAsciiString(p.stem().string());
	if (!stem.empty())
	{
		auto itStem = g_effectTextureByStemPreferred.find(stem);
		if (itStem != g_effectTextureByStemPreferred.end())
			return itStem->second;
	}

	auto it = g_effectTextureByFile.find(fileOnly);
	if (it != g_effectTextureByFile.end())
		return it->second;
	return "";
}

static int scoreEffectTextureName(const std::string &pathOrName)
{
	std::string s = toLowerAsciiString(pathOrName);
	int score = 0;
	// Prefer richer sprites over small spark kernels.
	if (s.find("fire") != std::string::npos)
		score += 8;
	if (s.find("flame") != std::string::npos || s.find("smoke") != std::string::npos || s.find("magic") != std::string::npos)
		score += 6;
	if (s.find("glow") != std::string::npos || s.find("light") != std::string::npos)
		score += 3;
	if (s.find("blur") != std::string::npos || s.find("ball") != std::string::npos)
		score -= 3;
	return score;
}

static unsigned int loadParticleTexture2D(const std::string &path)
{
	if (path.empty())
		return 0;
	int w = 0, h = 0, n = 0;
	unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!pixels || w <= 0 || h <= 0)
	{
		if (pixels)
			stbi_image_free(pixels);
		return 0;
	}

	unsigned int tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	stbi_image_free(pixels);
	return tex;
}

static std::unordered_map<std::string, std::string> g_spellIconByLower;
static bool g_spellIconIndexBuilt = false;

static void buildSpellIconIndex()
{
	if (g_spellIconIndexBuilt)
		return;
	g_spellIconIndexBuilt = true;

	const std::vector<std::string> roots = {
		"oac1osp/data/ui/icons/spell",
		"data/ui/icons/spell",
	};

	for (const std::string &root : roots)
	{
		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
			continue;
		for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(root, ec))
		{
			if (ec || !entry.is_regular_file())
				continue;
			std::string ext = toLowerAsciiString(entry.path().extension().string());
			if (ext != ".png" && ext != ".tga" && ext != ".dds")
				continue;
			std::string key = toLowerAsciiString(entry.path().filename().string());
			if (!key.empty() && g_spellIconByLower.find(key) == g_spellIconByLower.end())
				g_spellIconByLower[key] = entry.path().string();
		}
	}
}

static std::string resolveSpellIconPath(const std::string &iconName)
{
	if (iconName.empty())
		return "";
	std::error_code ec;
	if (std::filesystem::exists(iconName, ec))
		return iconName;

	buildSpellIconIndex();
	std::string key = toLowerAsciiString(std::filesystem::path(iconName).filename().string());
	auto it = g_spellIconByLower.find(key);
	if (it != g_spellIconByLower.end())
		return it->second;
	return "";
}

static std::vector<std::string> collectSpellIconNamesFromTable(const std::string &tablePath)
{
	std::vector<std::string> out;
	std::unordered_set<std::string> seen;
	std::ifstream file(tablePath, std::ios::binary);
	if (!file)
		return out;

	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::string text;
	text.resize(bytes.size());
	for (size_t i = 0; i < bytes.size(); i++)
	{
		unsigned char c = bytes[i];
		text[i] = (c >= 32 && c <= 126) ? (char)c : ' ';
	}

	std::regex iconRegex("(icon_(?:spell|debuff)_[a-z0-9_]+\\.png)", std::regex::icase);
	for (std::sregex_iterator it(text.begin(), text.end(), iconRegex), end; it != end; ++it)
	{
		std::string icon = (*it)[1].str();
		std::string lower = toLowerAsciiString(icon);
		if (seen.insert(lower).second)
			out.push_back(icon);
	}
	return out;
}

static unsigned int loadUiIconTexture(const std::string &path)
{
	int w = 0, h = 0, n = 0;
	unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!pixels || w <= 0 || h <= 0)
	{
		if (pixels)
			stbi_image_free(pixels);
		return 0;
	}

	unsigned int tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	stbi_image_free(pixels);
	return tex;
}

static void loadSpellHotbarIcons()
{
	if (!hotbarIconTextures.empty())
	{
		glDeleteTextures((GLsizei)hotbarIconTextures.size(), hotbarIconTextures.data());
		hotbarIconTextures.clear();
	}
	hotbarIconNames.clear();

	std::vector<std::string> iconNames = collectSpellIconNamesFromTable("oac1osp/data/tables/spell_new.tbl");
	std::vector<std::string> talentIconNames = collectSpellIconNamesFromTable("oac1osp/data/tables/spell_talent.tbl");
	std::unordered_set<std::string> seen;
	for (const std::string &name : iconNames)
		seen.insert(toLowerAsciiString(name));
	for (const std::string &name : talentIconNames)
	{
		std::string lower = toLowerAsciiString(name);
		if (seen.insert(lower).second)
			iconNames.push_back(name);
	}

	for (const std::string &name : iconNames)
	{
		std::string path = resolveSpellIconPath(name);
		if (path.empty())
			continue;
		unsigned int tex = loadUiIconTexture(path);
		if (tex == 0)
			continue;
		hotbarIconTextures.push_back(tex);
		hotbarIconNames.push_back(std::filesystem::path(path).filename().string());
		if (hotbarIconTextures.size() >= 64)
			break;
	}

	// Fallback numeric icons if table extraction fails.
	if (hotbarIconTextures.empty())
	{
		for (int i = 1; i <= HOTBAR_SLOT_COUNT; i++)
		{
			std::string name = std::to_string(i) + ".png";
			std::string path = resolveSpellIconPath(name);
			if (path.empty())
				continue;
			unsigned int tex = loadUiIconTexture(path);
			if (tex == 0)
				continue;
			hotbarIconTextures.push_back(tex);
			hotbarIconNames.push_back(name);
		}
	}

	for (int i = 0; i < HOTBAR_SLOT_COUNT; i++)
	{
		hotbarCooldownStartTimes[i] = -1000.0f;
		hotbarCooldownDurations[i] = 0.0f;
	}
}

static std::string resolveUiClickSfxPath()
{
	const std::vector<std::string> candidates = {
		"oac1osp/data/sound/sfx_ui_menu_skill_slot.wav",
		"oac1osp/data/sound/sfx_ui_menu_select.wav",
		"oac1osp/data/sound/sfx_ui_menu_confirm.wav",
		"data/sound/sfx_ui_menu_skill_slot.wav",
		"data/sound/sfx_ui_menu_select.wav",
		"data/sound/sfx_ui_menu_confirm.wav",
	};
	for (const std::string &path : candidates)
	{
		std::error_code ec;
		if (std::filesystem::exists(path, ec))
			return path;
	}
	return "";
}

static std::string resolveUiUnavailableSfxPath()
{
	const std::vector<std::string> candidates = {
		"oac1osp/data/sound/sfx_ui_ingame_skillbar_skill_unavail.wav",
		"oac1osp/data/sound/sfx_ui_menu_back.wav",
		"data/sound/sfx_ui_ingame_skillbar_skill_unavail.wav",
		"data/sound/sfx_ui_menu_back.wav",
	};
	for (const std::string &path : candidates)
	{
		std::error_code ec;
		if (std::filesystem::exists(path, ec))
			return path;
	}
	return "";
}

static void triggerHotbarSlot(int slotIndex, float nowSeconds)
{
	if (slotIndex < 0 || slotIndex >= HOTBAR_SLOT_COUNT || hotbarIconTextures.empty())
		return;

	const float elapsed = nowSeconds - hotbarCooldownStartTimes[slotIndex];
	const float duration = hotbarCooldownDurations[slotIndex];
	bool onCooldown = (duration > 0.0f && elapsed >= 0.0f && elapsed < duration);
	if (onCooldown)
	{
		if (!hotbarUnavailableSfxPath.empty() && shortcutSound)
			ma_engine_play_sound(&shortcutSound->engine, hotbarUnavailableSfxPath.c_str(), NULL);
		return;
	}

	hotbarCooldownStartTimes[slotIndex] = nowSeconds;
	hotbarCooldownDurations[slotIndex] = 2.5f + 0.35f * (float)(slotIndex % 6);

	if (!hotbarClickSfxPath.empty() && shortcutSound)
		ma_engine_play_sound(&shortcutSound->engine, hotbarClickSfxPath.c_str(), NULL);
}

static std::string pickRandomTerrainMapPath()
{
	if (terrainMapPaths.empty())
		return "";
	static std::mt19937 rng((unsigned int)std::random_device{}());
	std::uniform_int_distribution<int> dist(0, (int)terrainMapPaths.size() - 1);
	return terrainMapPaths[dist(rng)];
}

static int findAnimationByTokens(const Model &model, const std::vector<std::string> &preferredTokens, const std::vector<std::string> &rejectTokens = {})
{
	for (int i = 0; i < (int)model.animationNames.size(); i++)
	{
		std::string name = toLowerAsciiString(model.animationNames[i]);
		bool rejected = false;
		for (const std::string &reject : rejectTokens)
		{
			if (!reject.empty() && name.find(reject) != std::string::npos)
			{
				rejected = true;
				break;
			}
		}
		if (rejected)
			continue;

		for (const std::string &tok : preferredTokens)
		{
			if (name.find(tok) != std::string::npos)
				return i;
		}
	}
	return -1;
}

static void refreshPlayerAnimationBindings(Model &model)
{
	static const std::vector<std::string> nonLocomotion = {
		"sit", "sleep", "kneel", "dead", "die", "hurt", "combat", "magic", "emote", "jump"};
	playerIdleAnimIndex = findAnimationByTokens(model, {"idle01", "idle02", "idle", "stand"}, nonLocomotion);
	playerWalkAnimIndex = findAnimationByTokens(model, {"walk_forward", "walk", "move_forward", "move"}, nonLocomotion);
	playerRunAnimIndex = findAnimationByTokens(model, {"run_normal", "run_spint", "sprint", "run"}, nonLocomotion);
}

static void setPlayerLocomotionAnimation(Model &model, bool moving, bool walking)
{
	if (!model.animationsLoaded || model.animationCount <= 0)
		return;

	int target = playerIdleAnimIndex;
	if (moving)
		target = walking ? playerWalkAnimIndex : playerRunAnimIndex;

	if (target < 0)
		target = moving ? ((playerRunAnimIndex >= 0) ? playerRunAnimIndex : playerIdleAnimIndex) : playerIdleAnimIndex;
	if (target < 0)
		target = 0;

	if (model.selectedAnimation != target)
	{
		model.selectedAnimation = target;
		model.resetAnimation();
	}
	model.animationPlaying = true;
}

static glm::vec3 findNearestCollisionFreeSpawn(const Terrain &terrain, const glm::vec3 &preferred)
{
	const float radius = 0.45f;
	const float halfHeight = 0.9f;

	auto isFree = [&](float x, float z, glm::vec3 *outPos = nullptr) -> bool
	{
		x = std::clamp(x, terrain.minX, terrain.maxX);
		z = std::clamp(z, terrain.minZ, terrain.maxZ);
		float groundY = terrain.sampleHeightAt(x, z);
		float capsuleY = groundY + halfHeight;
		bool blocked = terrain.collidesWithPhysics(x, capsuleY, z, radius, halfHeight);
		if (!blocked && outPos)
			*outPos = glm::vec3(x, groundY, z);
		return !blocked;
	};

	glm::vec3 p;
	if (isFree(preferred.x, preferred.z, &p))
		return p;

	const float step = 2.5f;
	const int maxRing = 18;
	for (int ring = 1; ring <= maxRing; ring++)
	{
		float d = ring * step;
		for (int i = 0; i < 16; i++)
		{
			float t = (2.0f * glm::pi<float>() * (float)i) / 16.0f;
			float x = preferred.x + std::cos(t) * d;
			float z = preferred.z + std::sin(t) * d;
			if (isFree(x, z, &p))
				return p;
		}
	}

	// Last resort: center of map.
	float cx = 0.5f * (terrain.minX + terrain.maxX);
	float cz = 0.5f * (terrain.minZ + terrain.maxZ);
	float cy = terrain.sampleHeightAt(cx, cz);
	return glm::vec3(cx, cy, cz);
}

static void resetPlayerSpawn(const Terrain &terrain)
{
	glm::vec3 preferred(0.5f * (terrain.minX + terrain.maxX), 0.0f, 0.5f * (terrain.minZ + terrain.maxZ));
	std::string terrainName = terrain.fileName;
	size_t dot = terrainName.rfind('.');
	if (dot != std::string::npos)
		terrainName = terrainName.substr(0, dot);
	if (auto it = terrainSpawnPos.find(terrainName); it != terrainSpawnPos.end())
	{
		auto [pos, pitch, yaw] = it->second;
		preferred = pos;
		thirdPersonYaw = yaw;
		thirdPersonPitch = std::clamp(pitch, -60.0f, 35.0f);
	}

	playerWorldPos = findNearestCollisionFreeSpawn(terrain, preferred);
	playerVerticalVelocity = 0.0f;
	playerOnGround = true;
}

static void updateThirdPersonCamera()
{
	float eyeHeight = std::max(1.8f, playerModelScale * 1.2f) + cameraFollowHeight;
	glm::vec3 target = playerWorldPos + glm::vec3(0.0f, playerModelYOffset + eyeHeight, 0.0f);
	ourCamera.Yaw = thirdPersonYaw;
	ourCamera.Pitch = thirdPersonPitch;
	ourCamera.updateCameraVectors();
	// Keep camera locked on the character target in true third-person orbit mode.
	ourCamera.Position = target - ourCamera.Front * cameraFollowDistance;
}

static void setRmbLookCapture(GLFWwindow *window, bool capture)
{
	if (capture == rmbLookCaptureActive)
		return;

	glfwSetInputMode(window, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	if (glfwRawMouseMotionSupported())
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, capture ? GLFW_TRUE : GLFW_FALSE);

	rmbLookCaptureActive = capture;

	// Re-sync deltas when switching cursor capture mode.
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
	lastX = xpos;
	lastY = ypos;
	firstMouse = true;
}

static void updateRmbLookCapture(GLFWwindow *window, bool hasPlayableCharacter)
{
	bool wantsCapture = hasPlayableCharacter &&
						!fileDialogOpen &&
						(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
	setRmbLookCapture(window, wantsCapture);
}

static bool computeModelLocalBounds(const Model &model, glm::vec3 &outMin, glm::vec3 &outMax)
{
	if (model.vertices.empty())
		return false;

	glm::vec3 bmin(std::numeric_limits<float>::max());
	glm::vec3 bmax(std::numeric_limits<float>::lowest());
	for (const Vertex &v : model.vertices)
	{
		bmin = glm::min(bmin, v.PosCoords);
		bmax = glm::max(bmax, v.PosCoords);
	}

	if (bmax.y <= bmin.y)
		return false;

	outMin = bmin;
	outMax = bmax;
	return true;
}

static float computeTransformedHeightY(const glm::vec3 &localMin, const glm::vec3 &localMax, const glm::mat4 &transform)
{
	float minY = std::numeric_limits<float>::max();
	float maxY = std::numeric_limits<float>::lowest();
	for (int xi = 0; xi < 2; xi++)
	{
		for (int yi = 0; yi < 2; yi++)
		{
			for (int zi = 0; zi < 2; zi++)
			{
				glm::vec3 p(
					xi ? localMax.x : localMin.x,
					yi ? localMax.y : localMin.y,
					zi ? localMax.z : localMin.z);
				glm::vec4 world = transform * glm::vec4(p, 1.0f);
				minY = std::min(minY, world.y);
				maxY = std::max(maxY, world.y);
			}
		}
	}
	return std::max(0.0f, maxY - minY);
}

static bool autoCalibratePlayerScale(const Terrain &terrain, const Model &playerModel)
{
	glm::vec3 playerMin(0.0f), playerMax(0.0f);
	if (!computeModelLocalBounds(playerModel, playerMin, playerMax))
		return false;

	float nativeHeight = playerMax.y - playerMin.y;
	if (nativeHeight <= 0.01f)
		return false;

	std::unordered_map<const Model *, std::pair<glm::vec3, glm::vec3>> boundsCache;
	std::vector<float> nearbyHeights;
	const float sampleRadius = 90.0f;
	const float sampleRadiusSq = sampleRadius * sampleRadius;

	for (const auto &row : terrain.tiles)
	{
		for (const TileTerrain *tile : row)
		{
			if (!tile)
				continue;

			for (const auto &entry : tile->models)
			{
				const std::shared_ptr<Model> &propModel = entry.first;
				const glm::mat4 &propTransform = entry.second;
				if (!propModel || !propModel->modelLoaded)
					continue;

				glm::vec3 propPos(propTransform[3].x, propTransform[3].y, propTransform[3].z);
				glm::vec2 d2(propPos.x - playerWorldPos.x, propPos.z - playerWorldPos.z);
				if (glm::dot(d2, d2) > sampleRadiusSq)
					continue;

				auto cacheIt = boundsCache.find(propModel.get());
				if (cacheIt == boundsCache.end())
				{
					glm::vec3 bmin, bmax;
					if (!computeModelLocalBounds(*propModel, bmin, bmax))
						continue;
					cacheIt = boundsCache.emplace(propModel.get(), std::make_pair(bmin, bmax)).first;
				}

				float worldHeight = computeTransformedHeightY(cacheIt->second.first, cacheIt->second.second, propTransform);
				if (worldHeight >= 0.6f && worldHeight <= 40.0f)
					nearbyHeights.push_back(worldHeight);
			}
		}
	}

	float referenceHeight = 2.0f;
	if (!nearbyHeights.empty())
	{
		std::sort(nearbyHeights.begin(), nearbyHeights.end());
		size_t idx = (size_t)std::floor((nearbyHeights.size() - 1) * 0.35f);
		referenceHeight = nearbyHeights[idx];
	}

	float desiredCharacterHeight = std::clamp(referenceHeight * 0.60f, 1.4f, 3.6f);
	float calibratedScale = desiredCharacterHeight / nativeHeight;
	playerModelScale = std::clamp(calibratedScale, 0.8f, 16.0f);

	autoScaleLastNativeHeight = nativeHeight;
	autoScaleLastReferenceHeight = referenceHeight;
	autoScaleLastSampleCount = (int)nearbyHeights.size();
	return true;
}

int main(int argc, char **argv)
{
	// initialize and configure (use core profile mode and OpenGL v3.3)
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// GLFW window creation
	GLFWwindow *window = glfwCreateWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "BDAE 3D Model Viewer v.1.4", NULL, NULL);

	// set window icon
	int width, height, nrChannels;
	unsigned char *data = stbi_load("aux_docs/app icon.png", &width, &height, &nrChannels, 0);
	GLFWimage icon;
	icon.width = width;
	icon.height = height;
	icon.pixels = data;
	glfwSetWindowIcon(window, 1, &icon);
	stbi_image_free(data);

	// set OpenGL context and callback
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetScrollCallback(window, scroll_callback);	  // register mouse wheel callback
	glfwSetCursorPosCallback(window, mouse_callback); // register mouse movement callback
	glfwSetKeyCallback(window, key_callback);		  // register key callback

	// load all OpenGL function pointers
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	// setup settings panel (Dear ImGui library)
	ImGui::CreateContext();
	ImGui_ImplOpenGL3_Init("#version 330");
	ImGui_ImplGlfw_InitForOpenGL(window, true);

	ImGui::GetIO().IniFilename = NULL; // disable saving UI states to .ini file

	// apply styles to have a grayscale theme
	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;											  // border radius
	style.WindowBorderSize = 0.0f;											  // border width
	style.Colors[ImGuiCol_Text] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);			  // text color
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);		  // background color of the panel's main content area
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);	  // background color of the panel's title bar
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // .. (when panel is hidden)
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);		  // .. (when panel is overlayed and inactive)
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);		  // background color of input fields, checkboxes
	style.Colors[ImGuiCol_Button] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);			  // background color of buttons
	style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);		  // mark color in checkboxes
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);		  // background color of sliders
	style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.65f, 0.65f, 0.65f, 1.0f); // background color of table headers (for file browsing dialog)
	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);	  // background color of scrollbar tracks (for file browsing dialog)
	style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.65f, 0.65f, 0.65f, 1.0f); // background color of scrollbar thumbs (for file browsing dialog)
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);		  // background color of tooltips (for file browsing dialog)

	// configure file browsing dialog
	IGFD::FileDialogConfig cfg;
	cfg.path = "./data/model";															   // default path
	cfg.fileName = "";																	   // default file name (none)
	cfg.filePathName = "";																   // default file path name (none)
	cfg.countSelectionMax = 1;															   // only allow to select one file
	cfg.flags = ImGuiFileDialogFlags_HideColumnType | ImGuiFileDialogFlags_HideColumnDate; // flags: hide file type and date columns
	cfg.userFileAttributes = NULL;														   // no custom columns
	cfg.userDatas = NULL;																   // no custom user data passed to the dialog
	cfg.sidePane = NULL;																   // no side panel
	cfg.sidePaneWidth = 0.0f;															   // side panel width (unused)

	// load button icons
	unsigned int playIcon, stopIcon, switchIcon;

	data = stbi_load("aux_docs/button_play.png", &width, &height, &nrChannels, 0);
	glGenTextures(1, &playIcon);
	glBindTexture(GL_TEXTURE_2D, playIcon);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	stbi_image_free(data);

	data = stbi_load("aux_docs/button_stop.png", &width, &height, &nrChannels, 0);
	glGenTextures(1, &stopIcon);
	glBindTexture(GL_TEXTURE_2D, stopIcon);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	stbi_image_free(data);

	data = stbi_load("aux_docs/button_switch.png", &width, &height, &nrChannels, 0);
	glGenTextures(1, &switchIcon);
	glBindTexture(GL_TEXTURE_2D, switchIcon);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	stbi_image_free(data);

	// enable depth testing to ensure correct pixel rendering order in 3D space (depth buffer prevents incorrect overlaying and redrawing of objects)
	glEnable(GL_DEPTH_TEST);

	glEnable(GL_BLEND);								   // enable blending with the scene
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // use the opacity value of the model texture to blend it correctly, ensuring smooth transparency on the edges

	Light ourLight;

	Sound ourSound;
	shortcutSound = &ourSound;
	buildArmorModelList();
	buildTerrainMapList();
	buildRacePresetList();
	loadSpellHotbarIcons();
	hotbarClickSfxPath = resolveUiClickSfxPath();
	hotbarUnavailableSfxPath = resolveUiUnavailableSfxPath();

	Model bdaeModel("shaders/model.vs", "shaders/model.fs");

	Terrain terrainModel(ourCamera, ourLight);

	AppContext appContext;
	appContext.model = &bdaeModel;
	appContext.light = &ourLight;
	appContext.terrain = &terrainModel;

	glfwSetWindowUserPointer(window, &appContext);

	// optional CLI mode: load a .bdae model or .trn terrain directly at startup
	if (argc > 1)
	{
		std::filesystem::path inputPath(argv[1]);
		std::error_code ec;
		std::filesystem::path targetPath = std::filesystem::absolute(inputPath, ec);

		if (!ec && std::filesystem::exists(targetPath))
		{
			std::string ext = targetPath.extension().string();
			for (char &c : ext)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

			if (ext == ".bdae")
			{
				bdaeModel.load(targetPath.string().c_str(), ourSound, false);
				syncArmorModelIndex(targetPath.string());
				cfg.path = targetPath.parent_path().string();
			}
			else if (ext == ".trn")
			{
				isTerrainViewer = true;
				terrainModel.load(targetPath.string().c_str(), ourSound);
				cfg.path = targetPath.parent_path().string();
			}
			else
			{
				std::cerr << "[Startup] Skipping direct load: unsupported extension '" << ext << "'.\n";
			}
		}
		else
		{
			std::cerr << "[Startup] Skipping direct load: '" << inputPath.string()
					  << "' is not an existing file.\n";
		}
	}

	// game loop
	while (!glfwWindowShouldClose(window))
	{
		// per-frame time logic
		float currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		bool hasPlayableCharacter = gameModeEnabled && isTerrainViewer && characterControlEnabled &&
									bdaeModel.modelLoaded && terrainModel.terrainLoaded;
		updateRmbLookCapture(window, hasPlayableCharacter);

		// handle keyboard input
		if (!fileDialogOpen)
			processInput(window);

		// prepare ImGui for a new frame
		// _____________________________

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// define settings panel with dynamic size and fixed position
		ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 270.0f), ImVec2(200.0f, FLT_MAX));
		ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_None);

		settingsPanelHovered = ImGui::GetIO().WantCaptureMouse;

		ImGui::Begin("Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);

		// add a button that opens file browsing dialog
		if (ImGui::Button(isTerrainViewer ? "Load Terrain (beta)" : "Load Model"))
		{
			cfg.path = isTerrainViewer ? "./data/terrain" : cfg.path;

			IGFD::FileDialog::Instance()->OpenDialog(
				"File_Browsing_Dialog",							// dialog ID (used to reference this dialog instance)
				isTerrainViewer ? "Load Map" : "Load 3D Model", // dialog title
				isTerrainViewer ? ".trn" : ".bdae",				// file extension filter
				cfg												// config
			);
		}

		ImGui::SameLine();

		// define viewer mode change button
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));

		if (ImGui::ImageButton("##viewer_mode_change_button", switchIcon, ImVec2(25, 25)))
		{
			isTerrainViewer ? terrainModel.reset() : bdaeModel.reset();
			isTerrainViewer = !isTerrainViewer;
			cfg.path = isTerrainViewer ? "./data/terrain" : "./data/model";
			ma_sound_stop(&ourSound.sound);
		}

		ImGui::PopStyleColor(3);

		if (ImGui::Button("Quick Start Adventure"))
		{
			isTerrainViewer = true;
			gameModeEnabled = true;
			std::string randomMap = pickRandomTerrainMapPath();
			if (!randomMap.empty())
			{
				terrainModel.load(randomMap.c_str(), ourSound);
				cfg.path = std::filesystem::path(randomMap).parent_path().string();
			}

			if (!racePresets.empty())
			{
				bdaeModel.load(racePresets[selectedRacePreset].modelPath.c_str(), ourSound, false);
				refreshPlayerAnimationBindings(bdaeModel);
				setPlayerLocomotionAnimation(bdaeModel, false, true);
			}

			if (terrainModel.terrainLoaded)
			{
				resetPlayerSpawn(terrainModel);
				if (autoPlayerScaleEnabled && bdaeModel.modelLoaded)
					autoCalibratePlayerScale(terrainModel, bdaeModel);
				playerYawDegrees = thirdPersonYaw;
				updateThirdPersonCamera();
			}
		}

		// define file browsing dialog with fixed size and position in the center
		ImVec2 dialogSize(currentWindowWidth * 0.7f, currentWindowHeight * 0.6f);
		ImVec2 dialogPos((currentWindowWidth - dialogSize.x) * 0.5f, (currentWindowHeight - dialogSize.y) * 0.5f);
		ImGui::SetNextWindowSize(dialogSize, ImGuiCond_Always);
		ImGui::SetNextWindowPos(dialogPos, ImGuiCond_Always);

		fileDialogOpen = IGFD::FileDialog::Instance()->IsOpened("File_Browsing_Dialog");

		// if the dialog is opened with the load button, show it
		if (IGFD::FileDialog::Instance()->Display("File_Browsing_Dialog", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
		{
			// if selection is confirmed (by OK or double-click), process it
			if (IGFD::FileDialog::Instance()->IsOk())
			{
				std::map<std::string, std::string> selection = IGFD::FileDialog::Instance()->GetSelection(); // returns pairs (file name, full path)

				if (!isTerrainViewer)
				{
					bdaeModel.load(selection.begin()->second.c_str(), ourSound, isTerrainViewer);
					syncArmorModelIndex(selection.begin()->second);
				}
				else
					terrainModel.load(selection.begin()->second.c_str(), ourSound);
			}

			cfg.path = ImGuiFileDialog::Instance()->GetCurrentPath(); // save most recent path
			IGFD::FileDialog::Instance()->Close();					  // close the dialog after handling OK or Cancel
		}

		if (isTerrainViewer)
		{
			ImGui::Spacing();
			ImGui::SeparatorText("Adventure");
			ImGui::Checkbox("Game Mode", &gameModeEnabled);

			if (!racePresets.empty())
			{
				const char *currentRace = racePresets[selectedRacePreset].label.c_str();
				if (ImGui::BeginCombo("Race", currentRace))
				{
					for (int i = 0; i < (int)racePresets.size(); i++)
					{
						bool selected = (i == selectedRacePreset);
						if (ImGui::Selectable(racePresets[i].label.c_str(), selected))
							selectedRacePreset = i;
					}
					ImGui::EndCombo();
				}
			}

			if (ImGui::Button("Load Random Map + Race"))
			{
				gameModeEnabled = true;
				std::string randomMap = pickRandomTerrainMapPath();
				if (!randomMap.empty())
				{
					terrainModel.load(randomMap.c_str(), ourSound);
					cfg.path = std::filesystem::path(randomMap).parent_path().string();
				}

				if (!racePresets.empty())
				{
					bdaeModel.load(racePresets[selectedRacePreset].modelPath.c_str(), ourSound, false);
					refreshPlayerAnimationBindings(bdaeModel);
					setPlayerLocomotionAnimation(bdaeModel, false, true);
				}

				if (terrainModel.terrainLoaded)
				{
					resetPlayerSpawn(terrainModel);
					if (autoPlayerScaleEnabled && bdaeModel.modelLoaded)
						autoCalibratePlayerScale(terrainModel, bdaeModel);
					playerYawDegrees = thirdPersonYaw;
					updateThirdPersonCamera();
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Respawn Center") && terrainModel.terrainLoaded)
			{
				resetPlayerSpawn(terrainModel);
				playerYawDegrees = thirdPersonYaw;
				if (gameModeEnabled)
					updateThirdPersonCamera();
			}

			ImGui::Checkbox("Character Control", &characterControlEnabled);
			ImGui::Checkbox("Follow Camera", &cameraFollowEnabled);
			ImGui::Checkbox("Auto Scale", &autoPlayerScaleEnabled);
			ImGui::SameLine();
			if (ImGui::Button("Auto Calibrate Scale") && terrainModel.terrainLoaded && bdaeModel.modelLoaded)
			{
				autoCalibratePlayerScale(terrainModel, bdaeModel);
				if (gameModeEnabled)
					updateThirdPersonCamera();
			}
			ImGui::SliderFloat("Walk Speed", &playerWalkSpeed, 2.0f, 14.0f, "%.1f");
			ImGui::SliderFloat("Run Speed", &playerRunSpeed, 6.0f, 28.0f, "%.1f");
			ImGui::SliderFloat("Cam Dist", &cameraFollowDistance, 8.0f, 120.0f, "%.1f");
			ImGui::SliderFloat("Cam Height", &cameraFollowHeight, 1.0f, 20.0f, "%.1f");
			ImGui::SliderFloat("Jump Speed", &playerJumpVelocity, 6.0f, 22.0f, "%.1f");
			ImGui::SliderFloat("Model Scale", &playerModelScale, 1.0f, 14.0f, "%.1f");
			ImGui::SliderFloat("Model Y Offset", &playerModelYOffset, -8.0f, 8.0f, "%.1f");
			ImGui::SliderFloat("Facing Yaw Offset", &playerFacingYawOffset, -180.0f, 180.0f, "%.0f");
			if (autoScaleLastNativeHeight > 0.0f)
				ImGui::Text("AutoScale: native %.2f, ref %.2f, samples %d", autoScaleLastNativeHeight, autoScaleLastReferenceHeight, autoScaleLastSampleCount);
			ImGui::Text("Controls: WASD move, Shift walk, Space jump, RMB hold rotate camera");
		}

		// if a model is loaded, show its info and settings
		if (bdaeModel.modelLoaded && !isTerrainViewer)
		{
			ImGui::Spacing();
			ImGui::TextWrapped("File:\xC2\xA0%s", bdaeModel.fileName.c_str());
			if (currentArmorModelIndex >= 0 && !armorModelPaths.empty())
			{
				ImGui::Text("Armor: %d / %d (Left/Right)", currentArmorModelIndex + 1, (int)armorModelPaths.size());
				ImGui::Text("Jump: Home/End");
				ImGui::TextWrapped("Armor path: %s", armorModelPaths[currentArmorModelIndex].c_str());
			}
			ImGui::Text("Size: %d Bytes", bdaeModel.fileSize);
			ImGui::Text("Vertices: %d", bdaeModel.vertexCount);
			ImGui::Text("Faces: %d", bdaeModel.faceCount);
			ImGui::NewLine();
			ImGui::Checkbox("Base Mesh On/Off", &displayBaseMesh);
			ImGui::Spacing();
			ImGui::Checkbox("Lighting On/Off", &ourLight.showLighting);
			ImGui::Checkbox("Spell Hotbar", &hotbarEnabled);
			ImGui::NewLine();
			ImGui::Text("Alternative colors: %d", bdaeModel.alternativeTextureCount);
			ImGui::Spacing();

			if (bdaeModel.alternativeTextureCount > 0)
			{
				ImGui::PushItemWidth(130.0f);
				ImGui::SliderInt(" Color", &bdaeModel.selectedTexture, 0, bdaeModel.alternativeTextureCount);
				ImGui::PopItemWidth();
			}

			if (bdaeModel.animationsLoaded)
			{
				ImGui::Spacing();
				ImGui::Text("Animations: %d", bdaeModel.animationCount);
				ImGui::Spacing();

				if (bdaeModel.animationCount > 1)
				{
					ImGui::PushItemWidth(130.0f);

					if (ImGui::SliderInt("##animation_selector", &bdaeModel.selectedAnimation, 0, bdaeModel.animationCount - 1))
					{
						bdaeModel.animationPlaying = false;
						bdaeModel.resetAnimation();
					}

					ImGui::PopItemWidth();
				}

				ImGui::SameLine();

				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));

				if (ImGui::ImageButton("##animation_play_button", bdaeModel.animationPlaying ? stopIcon : playIcon, ImVec2(25, 25)))
				{
					if (!bdaeModel.animationPlaying)
						bdaeModel.animationPlaying = true;
					else
						bdaeModel.animationPlaying = false;
				}

				ImGui::PopStyleColor(3);

				// Debug readout to verify clip selection and timeline progression.
				if (bdaeModel.selectedAnimation >= 0 && bdaeModel.selectedAnimation < (int)bdaeModel.animations.size())
				{
					std::string clipName = (bdaeModel.selectedAnimation < (int)bdaeModel.animationNames.size())
											   ? bdaeModel.animationNames[bdaeModel.selectedAnimation]
											   : std::string("<unnamed>");
					float clipDuration = bdaeModel.animations[bdaeModel.selectedAnimation].first;

					ImGui::TextWrapped("Clip: %s", clipName.c_str());
					ImGui::Text("State: %s", bdaeModel.animationPlaying ? "Playing" : "Paused");
					ImGui::Text("Time: %.2f / %.2f", bdaeModel.currentAnimationTime, clipDuration);
				}
			}

			ourSound.updateSoundUI(bdaeModel.sounds, playIcon, stopIcon);
		}

		if (terrainModel.terrainLoaded && isTerrainViewer)
		{
			ImGui::Spacing();
			ImGui::TextWrapped("File:\xC2\xA0%s", terrainModel.fileName.c_str());

			// [currently disabled]
			// ImGui::Text("Size: %d Bytes", terrainModel.fileSize);
			// ImGui::Text("Vertices: %d", terrainModel.vertexCount);
			// ImGui::Text("Faces: %d", terrainModel.faceCount);

			ImGui::Text("3D Models: %d", terrainModel.modelCount);
			ImGui::NewLine();
			ImGui::Checkbox("Base Mesh (K)", &displayBaseMesh);
			ImGui::Spacing();

			// ImGui::Checkbox("Walkable (N)", &displayNavMesh);
			// ImGui::Spacing();
			// ImGui::Checkbox("Physics (M)", &displayPhysics);
			// ImGui::Spacing();

			ImGui::Checkbox("Lighting (L)", &ourLight.showLighting);
			ImGui::NewLine();
			ImGui::TextWrapped("Terrain: %d x %d tiles", terrainModel.tilesX, terrainModel.tilesZ);

			// ImGui::NewLine();
			// ImGui::TextWrapped("Pitch: %.2f, Yaw: %.2f", ourCamera.Pitch, ourCamera.Yaw);

			ImGui::Text("Position: (x, y, z)");
			ImGui::Spacing();

			ImGui::PushItemWidth(180.0f);
			ImGui::DragFloat3("##Camera Pos", &ourCamera.Position.x, 0.1f, -FLT_MAX, FLT_MAX, "%.0f");
			ImGui::PopItemWidth();

			ImGui::Text("x: min %d, max %d", (int)terrainModel.minX, (int)terrainModel.maxX);
			ImGui::Text("z: min %d, max %d", (int)terrainModel.minZ, (int)terrainModel.maxZ);

			ourSound.updateSoundUI(terrainModel.sounds, playIcon, stopIcon);
		}

		ImGui::End();

		if (hotbarEnabled && !hotbarIconTextures.empty())
		{
			const float slotSize = 46.0f;
			const float spacing = 6.0f;
			const float pad = 10.0f;
			const float barWidth = HOTBAR_SLOT_COUNT * slotSize + (HOTBAR_SLOT_COUNT - 1) * spacing + pad * 2.0f;
			const float barHeight = slotSize + pad * 2.0f;
			const ImVec2 barPos((currentWindowWidth - barWidth) * 0.5f, currentWindowHeight - barHeight - 18.0f);

			ImGui::SetNextWindowPos(barPos, ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(barWidth, barHeight), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.45f);

			ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
										ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
			ImGui::Begin("##SpellHotbar", NULL, barFlags);

			for (int i = 0; i < HOTBAR_SLOT_COUNT; i++)
			{
				if (i > 0)
					ImGui::SameLine(0.0f, spacing);

				int iconIndex = i % (int)hotbarIconTextures.size();
				unsigned int tex = hotbarIconTextures[iconIndex];
				ImGui::PushID(i);
				ImVec2 slotPos = ImGui::GetCursorScreenPos();

				if (ImGui::ImageButton("##hotbar_slot", (ImTextureID)(intptr_t)tex, ImVec2(slotSize, slotSize)))
				{
					triggerHotbarSlot(i, currentFrame);
				}

				ImDrawList *drawList = ImGui::GetWindowDrawList();
				drawList->AddRect(slotPos, ImVec2(slotPos.x + slotSize, slotPos.y + slotSize), IM_COL32(210, 180, 70, 255), 3.0f, 0, 2.0f);

				float elapsed = currentFrame - hotbarCooldownStartTimes[i];
				float duration = hotbarCooldownDurations[i];
				if (duration > 0.0f && elapsed >= 0.0f && elapsed < duration)
				{
					float ratio = 1.0f - (elapsed / duration);
					float h = slotSize * ratio;
					drawList->AddRectFilled(slotPos, ImVec2(slotPos.x + slotSize, slotPos.y + h), IM_COL32(18, 18, 18, 170), 3.0f);
					char cd[8];
					snprintf(cd, sizeof(cd), "%.1f", duration - elapsed);
					ImVec2 tsize = ImGui::CalcTextSize(cd);
					drawList->AddText(ImVec2(slotPos.x + (slotSize - tsize.x) * 0.5f, slotPos.y + (slotSize - tsize.y) * 0.5f), IM_COL32(255, 255, 255, 255), cd);
				}

				const char *keys = "1234567890-=";
				char keyLabel[4] = {keys[i], '\0', '\0', '\0'};
				drawList->AddText(ImVec2(slotPos.x + 3.0f, slotPos.y + slotSize - 16.0f), IM_COL32(250, 250, 210, 255), keyLabel);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", hotbarIconNames[iconIndex].c_str());
				}

				ImGui::PopID();
			}

			ImGui::End();
		}

		// _____________________________

		glClearColor(0.85f, 0.85f, 0.85f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the color buffer (fill the screen with a clear color) and the depth buffer; otherwise the information of the previous frame stays in these buffers

		// update dynamic shader uniforms on GPU
		glm::mat4 view = ourCamera.GetViewMatrix();
		glm::mat4 projection = glm::perspective(glm::radians(ourCamera.Zoom), (float)currentWindowWidth / (float)currentWindowHeight, 0.1f, 1000.0f);

		if (!isTerrainViewer && bdaeModel.modelLoaded)
		{
			bdaeModel.draw(glm::mat4(1.0f), view, projection, ourCamera.Position, deltaTime, ourLight.showLighting, displayBaseMesh); // render model

			ourLight.draw(view, projection); // render light cube
		}
		else if (terrainModel.terrainLoaded)
		{
			terrainModel.draw(view, projection, displayBaseMesh, displayNavMesh, displayPhysics, deltaTime); // render terrain

			if (bdaeModel.modelLoaded)
			{
				glm::mat4 playerModel = glm::translate(glm::mat4(1.0f), playerWorldPos + glm::vec3(0.0f, playerModelYOffset, 0.0f));
				playerModel = glm::rotate(playerModel, glm::radians(playerYawDegrees + playerFacingYawOffset), glm::vec3(0, 1, 0));
				playerModel = glm::scale(playerModel, glm::vec3(playerModelScale));
				bdaeModel.draw(playerModel, view, projection, ourCamera.Position, deltaTime, ourLight.showLighting, displayBaseMesh);
			}
		}

		// render settings panel (and file browsing dialog, if open)
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window); // make the contents of the back buffer (stores the completed frames) visible on the screen
		glfwPollEvents();		 // if any events are triggered (like keyboard input or mouse movement events), updates the window state, and calls the corresponding functions (which we can register via callback methods)
	}

	// terminate, clearing all previously allocated resources
	if (!hotbarIconTextures.empty())
		glDeleteTextures((GLsizei)hotbarIconTextures.size(), hotbarIconTextures.data());
	glfwTerminate();
	ma_engine_uninit(&ourSound.engine);
	return 0;
}

// whenever the window size changed (by OS or user resize), this callback function executes
void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
	glViewport(0, 0, width, height);
}

// whenever the mouse uses scroll wheel, this callback function executes
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
	if (fileDialogOpen)
		return;

	// handle mouse wheel scroll input using the Camera class function
	ourCamera.ProcessMouseScroll(yoffset);
}

// whenever the mouse moves, this callback function executes
void mouse_callback(GLFWwindow *window, double xpos, double ypos)
{
	if ((fileDialogOpen || settingsPanelHovered) && !rmbLookCaptureActive)
		return;

	// calculate the mouse offset since the last frame
	// (xpos and ypos are the current cursor coordinates in screen space)
	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; // reversed since y-coordinates range from bottom to top
	lastX = xpos;
	lastY = ypos;

	if (gameModeEnabled && isTerrainViewer)
	{
		if (rmbLookCaptureActive || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
		{
			thirdPersonYaw += xoffset * ourCamera.MouseSensitivity;
			thirdPersonPitch += yoffset * ourCamera.MouseSensitivity;
			thirdPersonPitch = std::clamp(thirdPersonPitch, -60.0f, 35.0f);
			if (cameraFollowEnabled)
				updateThirdPersonCamera();
			return;
		}
		return;
	}

	// only rotate the mesh if the right mouse button is pressed
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
	{
		AppContext *ctx = static_cast<AppContext *>(glfwGetWindowUserPointer(window));

		ctx->model->meshYaw += xoffset * meshRotationSensitivity;
		ctx->model->meshPitch += -yoffset * meshRotationSensitivity;
		return;
	}

	// only rotate the camera if the left mouse button is pressed
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)
		return;

	// skip camera rotation for the first frame to prevent a sudden jump or when
	if (firstMouse)
	{
		firstMouse = false;
		return;
	}

	// handle mouse movement input using the Camera class function
	ourCamera.ProcessMouseMovement(xoffset, yoffset);
}

// whenever a key is pressed, this callback function executes and only once, preventing continuous toggling when a key is held down (which would occur in processInput)
void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	AppContext *ctx = static_cast<AppContext *>(glfwGetWindowUserPointer(window));

	if (action == GLFW_PRESS)
	{
		switch (key)
		{
		case GLFW_KEY_K:
			displayBaseMesh = !displayBaseMesh;
			break;
		case GLFW_KEY_L:
			ctx->light->showLighting = !ctx->light->showLighting;
			break;
		case GLFW_KEY_N:
			displayNavMesh = !displayNavMesh;
			break;
		case GLFW_KEY_M:
			displayPhysics = !displayPhysics;
			break;
		case GLFW_KEY_F:
		{
			isFullscreen = !isFullscreen;
			if (isFullscreen)
			{
				// switch to fullscreen mode on primary monitor
				GLFWmonitor *monitor = glfwGetPrimaryMonitor();		 // main display in the system
				const GLFWvidmode *mode = glfwGetVideoMode(monitor); // video mode (info like resolution, color depth, refresh rate)
				glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);

				currentWindowWidth = mode->width;
				currentWindowHeight = mode->height;
			}
			else
			{
				// restore to windowed mode with default position + size
				glfwSetWindowMonitor(window, NULL, DEFAULT_WINDOW_POS_X, DEFAULT_WINDOW_POS_Y, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, 0);

				// reset mouse to avoid camera jump
				double xpos, ypos;
				glfwGetCursorPos(window, &xpos, &ypos);
				lastX = xpos;
				lastY = ypos;
				firstMouse = true;

				currentWindowWidth = DEFAULT_WINDOW_WIDTH;
				currentWindowHeight = DEFAULT_WINDOW_HEIGHT;
			}
		}
		break;
		case GLFW_KEY_1:
		case GLFW_KEY_2:
		case GLFW_KEY_3:
		case GLFW_KEY_4:
		case GLFW_KEY_5:
		case GLFW_KEY_6:
		case GLFW_KEY_7:
		case GLFW_KEY_8:
		case GLFW_KEY_9:
		case GLFW_KEY_0:
		case GLFW_KEY_MINUS:
		case GLFW_KEY_EQUAL:
		{
			if (!hotbarEnabled || hotbarIconTextures.empty())
				break;
			int slot = -1;
			switch (key)
			{
			case GLFW_KEY_1:
				slot = 0;
				break;
			case GLFW_KEY_2:
				slot = 1;
				break;
			case GLFW_KEY_3:
				slot = 2;
				break;
			case GLFW_KEY_4:
				slot = 3;
				break;
			case GLFW_KEY_5:
				slot = 4;
				break;
			case GLFW_KEY_6:
				slot = 5;
				break;
			case GLFW_KEY_7:
				slot = 6;
				break;
			case GLFW_KEY_8:
				slot = 7;
				break;
			case GLFW_KEY_9:
				slot = 8;
				break;
			case GLFW_KEY_0:
				slot = 9;
				break;
			case GLFW_KEY_MINUS:
				slot = 10;
				break;
			case GLFW_KEY_EQUAL:
				slot = 11;
				break;
			}
			if (slot >= 0)
				triggerHotbarSlot(slot, (float)glfwGetTime());
		}
		break;
		case GLFW_KEY_RIGHT:
		case GLFW_KEY_LEFT:
		case GLFW_KEY_HOME:
		case GLFW_KEY_END:
		{
			if (isTerrainViewer || !ctx->model || !shortcutSound || armorModelPaths.empty())
				break;
			if (currentArmorModelIndex < 0)
				currentArmorModelIndex = 0;
			int next = currentArmorModelIndex;
			if (key == GLFW_KEY_HOME)
				next = 0;
			else if (key == GLFW_KEY_END)
				next = (int)armorModelPaths.size() - 1;
			else
			{
				int delta = (key == GLFW_KEY_RIGHT) ? 1 : -1;
				next = currentArmorModelIndex + delta;
				if (next < 0)
					next = (int)armorModelPaths.size() - 1;
				if (next >= (int)armorModelPaths.size())
					next = 0;
			}
			ctx->model->load(armorModelPaths[next].c_str(), *shortcutSound, false);
			currentArmorModelIndex = next;
		}
		break;
		}
	}
}

// process all input: query GLFW whether relevant keys are pressed / released this frame and react accordingly
void processInput(GLFWwindow *window)
{
	AppContext *ctx = static_cast<AppContext *>(glfwGetWindowUserPointer(window));

	// Escape key to close the program
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	bool hasPlayableCharacter = gameModeEnabled && isTerrainViewer && characterControlEnabled && ctx && ctx->model && ctx->terrain &&
								ctx->model->modelLoaded && ctx->terrain->terrainLoaded;
	if (hasPlayableCharacter)
	{
		glm::vec3 move(0.0f);
		float yawRad = glm::radians(thirdPersonYaw);
		glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
		glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
			move += forward;
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
			move -= forward;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
			move -= right;
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
			move += right;

		bool moving = glm::length(move) > 0.0001f;
		bool walking = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
		float speed = walking ? playerWalkSpeed : playerRunSpeed;
		if (playerOnGround && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		{
			playerVerticalVelocity = playerJumpVelocity;
			playerOnGround = false;
		}

		if (moving)
		{
			glm::vec3 dir = glm::normalize(move);
			playerYawDegrees = glm::degrees(std::atan2(dir.x, dir.z));
			glm::vec3 oldPos = playerWorldPos;
			glm::vec3 candidate = oldPos + dir * speed * deltaTime;
			candidate.x = std::clamp(candidate.x, ctx->terrain->minX, ctx->terrain->maxX);
			candidate.z = std::clamp(candidate.z, ctx->terrain->minZ, ctx->terrain->maxZ);

			const float capsuleRadius = 0.45f;
			const float capsuleHalfHeight = 0.9f;

			float candidateGroundY = ctx->terrain->sampleHeightAt(candidate.x, candidate.z);
			float candidateCapsuleY = candidateGroundY + capsuleHalfHeight;
			bool blocked = ctx->terrain->collidesWithPhysics(candidate.x, candidateCapsuleY, candidate.z, capsuleRadius, capsuleHalfHeight);

			if (blocked)
			{
				glm::vec3 slideX = oldPos;
				slideX.x = candidate.x;
				float slideXGroundY = ctx->terrain->sampleHeightAt(slideX.x, slideX.z);
				float slideXCapsuleY = slideXGroundY + capsuleHalfHeight;
				if (!ctx->terrain->collidesWithPhysics(slideX.x, slideXCapsuleY, slideX.z, capsuleRadius, capsuleHalfHeight))
					candidate = slideX;
				else
				{
					glm::vec3 slideZ = oldPos;
					slideZ.z = candidate.z;
					float slideZGroundY = ctx->terrain->sampleHeightAt(slideZ.x, slideZ.z);
					float slideZCapsuleY = slideZGroundY + capsuleHalfHeight;
					if (!ctx->terrain->collidesWithPhysics(slideZ.x, slideZCapsuleY, slideZ.z, capsuleRadius, capsuleHalfHeight))
						candidate = slideZ;
					else
						candidate = oldPos;
				}
			}

			playerWorldPos = candidate;
		}
		float groundY = ctx->terrain->sampleHeightAt(playerWorldPos.x, playerWorldPos.z);
		if (!playerOnGround || playerVerticalVelocity > 0.0f)
		{
			playerVerticalVelocity -= playerGravity * deltaTime;
			playerWorldPos.y += playerVerticalVelocity * deltaTime;
		}

		if (playerWorldPos.y <= groundY)
		{
			playerWorldPos.y = groundY;
			playerVerticalVelocity = 0.0f;
			playerOnGround = true;
		}

		setPlayerLocomotionAnimation(*ctx->model, moving, walking);

		if (cameraFollowEnabled)
			updateThirdPersonCamera();
		return;
	}

	/*
	   WASD keys for camera movement:
	   W – move forward (along the camera's viewing direction vector, i.e. z-axis)
	   S – move backward
	   A – move left (along the right vector, i.e. x-axis; computed using the cross product)
	   D – move right
	*/
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		ourCamera.ProcessKeyboard(FORWARD);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		ourCamera.ProcessKeyboard(BACKWARD);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		ourCamera.ProcessKeyboard(LEFT);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		ourCamera.ProcessKeyboard(RIGHT);

	ourCamera.UpdatePosition(deltaTime);
}
