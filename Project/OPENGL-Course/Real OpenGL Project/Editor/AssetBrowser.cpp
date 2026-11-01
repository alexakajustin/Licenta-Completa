#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "Editor/AssetBrowser.h"
#include "Scene/SceneManager.h"
#include "Rendering/Material.h"
#include "Rendering/Mesh.h"
#include "Rendering/PrimitiveGenerator.h"
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"
#include "stb_image.h"
#include "imgui_internal.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace {
	const fs::path CACHE_DIR = "Cache/Thumbnails";
	constexpr size_t MAX_THUMB_TEXTURES = 1200; // About 75 MB of 128 px thumbnails
	constexpr int UPLOADS_PER_FRAME = 24;
	constexpr int WORKER_COUNT = 3;

	std::string Lower(std::string s)
	{
		for (auto& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	std::string Utf8(const fs::path& p)
	{
		try {
			auto u = p.u8string();
			return std::string(u.begin(), u.end());
		}
		catch (...) { return "?"; }
	}

	std::string Narrow(const fs::path& p)
	{
		try { return p.string(); }
		catch (...) { return std::string(); }
	}

	int64_t Stamp(const fs::path& p)
	{
		std::error_code ec;
		auto t = fs::last_write_time(p, ec);
		return ec ? 0 : (int64_t)t.time_since_epoch().count();
	}

	AssetType Classify(const fs::path& path, const std::string& ext)
	{
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") return AssetType::Texture;
		if (ext == ".obj" || ext == ".fbx" || ext == ".dae" || ext == ".gltf" || ext == ".glb") return AssetType::Model;
		if (ext == ".mat") return AssetType::MaterialAsset;
		if (ext == ".lua") return AssetType::Script;
		if (ext == ".vert" || ext == ".frag" || ext == ".geom" || ext == ".glsl" || ext == ".tesc" || ext == ".tese" || ext == ".comp") return AssetType::ShaderSource;
		if (ext == ".json") {
			for (const auto& part : path)
				if (Lower(Narrow(part)) == "scenes") return AssetType::Scene;
			return AssetType::Data;
		}
		if (ext == ".bin" || ext == ".mtl" || ext == ".txt" || ext == ".ini") return AssetType::Data;
		return AssetType::Other;
	}

	// One directory entry as an asset; false for what the browser hides (dot files, scene data)
	bool MakeInfo(const fs::directory_entry& entry, AssetInfo& info, bool showSceneData)
	{
		std::error_code ec;
		info.path = entry.path();
		info.pathStr = Narrow(info.path);
		info.name = Utf8(info.path.filename());
		if (info.pathStr.empty() || info.name.empty() || info.name[0] == '.') return false;
		info.lowerName = Lower(info.name);
		info.modified = Stamp(info.path);
		if (entry.is_directory(ec)) {
			info.type = AssetType::Folder;
			return true;
		}
		std::string ext = Lower(Narrow(info.path.extension()));
		// Engine caches: meshes and instances saved next to a scene, Assimp binary copies of models
		if (!showSceneData && (ext == ".mesh" || ext == ".inst" || ext == ".assbin")) return false;
		info.type = Classify(info.path, ext);
		info.size = entry.file_size(ec);
		if (ec) info.size = 0;
		return true;
	}

	const char* TypeName(AssetType t)
	{
		switch (t) {
		case AssetType::Folder:        return "Folder";
		case AssetType::Texture:       return "Texture";
		case AssetType::Model:         return "Model";
		case AssetType::MaterialAsset: return "Material";
		case AssetType::Scene:         return "Scene";
		case AssetType::Script:        return "Lua script";
		case AssetType::ShaderSource:  return "Shader";
		case AssetType::Data:          return "Data";
		default:                       return "File";
		}
	}

	ImU32 TypeColor(AssetType t)
	{
		switch (t) {
		case AssetType::Folder:        return IM_COL32(222, 170, 78, 255);
		case AssetType::Texture:       return IM_COL32(88, 176, 104, 255);
		case AssetType::Model:         return IM_COL32(80, 140, 220, 255);
		case AssetType::MaterialAsset: return IM_COL32(168, 104, 220, 255);
		case AssetType::Scene:         return IM_COL32(230, 120, 60, 255);
		case AssetType::Script:        return IM_COL32(210, 196, 70, 255);
		case AssetType::ShaderSource:  return IM_COL32(60, 180, 180, 255);
		default:                       return IM_COL32(130, 130, 140, 255);
		}
	}

	std::string FormatSize(uintmax_t bytes)
	{
		char buf[32];
		if (bytes < 1024) snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
		else if (bytes < 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
		else if (bytes < 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
		else snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
		return buf;
	}

	// File times are FILETIME ticks (100 ns since 1601) with MSVC's file clock
	std::string FormatTime(int64_t ticks)
	{
		if (ticks <= 0) return "";
		ULARGE_INTEGER u;
		u.QuadPart = (ULONGLONG)ticks;
		FILETIME ft{ u.LowPart, u.HighPart }, local;
		SYSTEMTIME st;
		if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return "";
		char buf[32];
		snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
		return buf;
	}

	uint64_t Fnv1a(const std::string& s)
	{
		uint64_t h = 1469598103934665603ull;
		for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
		return h;
	}

	// Shrinks an RGBA image to fit in maxSize (alpha-weighted box filter: transparent pixels do not
	// darken the edges of foliage)
	void Shrink(const unsigned char* src, int sw, int sh, int maxSize, std::vector<unsigned char>& out, int& ow, int& oh)
	{
		float scale = std::min(1.0f, (float)maxSize / (float)std::max(sw, sh));
		ow = std::max(1, (int)std::lround(sw * scale));
		oh = std::max(1, (int)std::lround(sh * scale));
		out.assign((size_t)ow * oh * 4, 0);
		for (int y = 0; y < oh; y++) {
			int y0 = (int)((int64_t)y * sh / oh), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * sh / oh));
			for (int x = 0; x < ow; x++) {
				int x0 = (int)((int64_t)x * sw / ow), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * sw / ow));
				double r = 0, g = 0, b = 0, a = 0;
				int n = 0;
				for (int yy = y0; yy < y1; yy++) {
					const unsigned char* p = src + ((size_t)yy * sw + x0) * 4;
					for (int xx = x0; xx < x1; xx++, p += 4) {
						double w = p[3] / 255.0;
						r += p[0] * w; g += p[1] * w; b += p[2] * w; a += p[3];
						n++;
					}
				}
				unsigned char* d = &out[((size_t)y * ow + x) * 4];
				double wa = a / 255.0;
				d[0] = (unsigned char)(wa > 0 ? std::min(255.0, r / wa) : 0);
				d[1] = (unsigned char)(wa > 0 ? std::min(255.0, g / wa) : 0);
				d[2] = (unsigned char)(wa > 0 ? std::min(255.0, b / wa) : 0);
				d[3] = (unsigned char)(a / n);
			}
		}
	}

	struct CacheHeader { char magic[4]; int32_t w, h, srcW, srcH; };

	bool ReadCache(const std::string& file, std::vector<unsigned char>& rgba, int& w, int& h, int& srcW, int& srcH)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in) return false;
		CacheHeader hd;
		if (!in.read((char*)&hd, sizeof(hd)) || std::memcmp(hd.magic, "RTH1", 4) != 0) return false;
		if (hd.w <= 0 || hd.h <= 0 || hd.w > 1024 || hd.h > 1024) return false;
		rgba.resize((size_t)hd.w * hd.h * 4);
		if (!in.read((char*)rgba.data(), (std::streamsize)rgba.size())) return false;
		w = hd.w; h = hd.h; srcW = hd.srcW; srcH = hd.srcH;
		return true;
	}

	void WriteCache(const std::string& file, const unsigned char* rgba, int w, int h, int srcW, int srcH)
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		if (!out) return;
		CacheHeader hd{ { 'R', 'T', 'H', '1' }, w, h, srcW, srcH };
		out.write((const char*)&hd, sizeof(hd));
		out.write((const char*)rgba, (std::streamsize)w * h * 4);
	}

	bool IsAncestorOrSelf(const fs::path& a, const fs::path& b)
	{
		auto ia = a.begin(), ib = b.begin();
		for (; ia != a.end(); ++ia, ++ib)
			if (ib == b.end() || Lower(Narrow(*ia)) != Lower(Narrow(*ib))) return false;
		return true;
	}

	// The longest start of s that fits in width (whole UTF-8 characters), with "..." when cut
	std::string Fit(const std::string& s, float width)
	{
		if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
		size_t lo = 0, hi = s.size();
		while (lo < hi) {
			size_t mid = (lo + hi + 1) / 2;
			std::string t = s.substr(0, mid) + "...";
			if (ImGui::CalcTextSize(t.c_str()).x <= width) lo = mid; else hi = mid - 1;
		}
		while (lo > 0 && lo < s.size() && ((unsigned char)s[lo] & 0xC0) == 0x80) lo--;
		return s.substr(0, lo) + "...";
	}

	// A name on up to two lines (broken after _ - . or a space when possible)
	void SplitName(const std::string& name, float width, std::string& line1, std::string& line2)
	{
		if (ImGui::CalcTextSize(name.c_str()).x <= width) { line1 = name; line2.clear(); return; }
		size_t fit = 0;
		while (fit < name.size()) {
			size_t next = fit + 1;
			while (next < name.size() && ((unsigned char)name[next] & 0xC0) == 0x80) next++;
			if (ImGui::CalcTextSize(name.substr(0, next).c_str()).x > width) break;
			fit = next;
		}
		size_t brk = fit;
		for (size_t i = fit; i > fit * 6 / 10 && i > 0; i--)
			if (std::strchr(" _-.", name[i - 1])) { brk = i; break; }
		if (brk == 0) brk = std::max<size_t>(fit, 1);
		line1 = name.substr(0, brk);
		line2 = Fit(name.substr(brk), width);
	}

	void DrawTypeIcon(ImDrawList* dl, ImVec2 a, ImVec2 b, AssetType type, const std::string& ext)
	{
		float w = std::min(b.x - a.x, b.y - a.y);
		ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
		ImVec2 o(c.x - w * 0.5f, c.y - w * 0.5f);
		if (type == AssetType::Folder) {
			ImU32 back = IM_COL32(176, 128, 52, 255), front = TypeColor(type);
			dl->AddRectFilled(ImVec2(o.x + w * 0.10f, o.y + w * 0.20f), ImVec2(o.x + w * 0.46f, o.y + w * 0.36f), back, w * 0.05f);
			dl->AddRectFilled(ImVec2(o.x + w * 0.10f, o.y + w * 0.28f), ImVec2(o.x + w * 0.90f, o.y + w * 0.84f), back, w * 0.06f);
			dl->AddRectFilled(ImVec2(o.x + w * 0.10f, o.y + w * 0.36f), ImVec2(o.x + w * 0.90f, o.y + w * 0.84f), front, w * 0.06f);
			return;
		}
		// A page with a folded corner and a coloured label of the extension
		ImVec2 p0(o.x + w * 0.22f, o.y + w * 0.10f), p1(o.x + w * 0.78f, o.y + w * 0.90f);
		float fold = w * 0.16f;
		ImU32 paper = IM_COL32(196, 200, 210, 255);
		dl->AddRectFilled(p0, ImVec2(p1.x - fold, p1.y), paper, w * 0.03f);
		dl->AddRectFilled(ImVec2(p1.x - fold, p0.y + fold), p1, paper, w * 0.03f);
		dl->AddTriangleFilled(ImVec2(p1.x - fold, p0.y), ImVec2(p1.x - fold, p0.y + fold), ImVec2(p1.x, p0.y + fold), IM_COL32(150, 154, 166, 255));
		std::string label = Lower(ext);
		if (!label.empty() && label[0] == '.') label = label.substr(1);
		for (auto& ch : label) ch = (char)std::toupper((unsigned char)ch);
		if (label.size() > 4) label.resize(4);
		ImVec2 band0(o.x + w * 0.14f, o.y + w * 0.56f), band1(o.x + w * 0.86f, o.y + w * 0.78f);
		dl->AddRectFilled(band0, band1, TypeColor(type), w * 0.03f);
		float size = std::max(9.0f, w * 0.15f);
		ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
		dl->AddText(ImGui::GetFont(), size, ImVec2((band0.x + band1.x - ts.x) * 0.5f, (band0.y + band1.y - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), label.c_str());
	}
}

// =====================================================================
// Setup
// =====================================================================

AssetBrowser::AssetBrowser() : currentAssetPath("Assets") {}

AssetBrowser::~AssetBrowser()
{
	StopSearch();
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		stopWorkers = true;
	}
	jobSignal.notify_all();
	for (auto& t : workers) if (t.joinable()) t.join();
	for (auto& [key, thumb] : thumbs) if (thumb.tex) glDeleteTextures(1, &thumb.tex);
	CleanupThumbnailFBO();
}

void AssetBrowser::Init()
{
	std::error_code ec;
	fs::create_directories(CACHE_DIR, ec);
	InitThumbnailFBO();
	StartWorkers();
	Navigate(root);
}

void AssetBrowser::InitThumbnailFBO()
{
	if (thumbnailFBO != 0) CleanupThumbnailFBO();
	glGenFramebuffers(1, &thumbnailFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, thumbnailFBO);
	glGenTextures(1, &thumbnailTexture);
	glBindTexture(GL_TEXTURE_2D, thumbnailTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, THUMB_SIZE, THUMB_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, thumbnailTexture, 0);
	glGenRenderbuffers(1, &thumbnailDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, thumbnailDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, THUMB_SIZE, THUMB_SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, thumbnailDepth);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("[AssetBrowser] Thumbnail framebuffer is not complete\n");
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	thumbnailShader.CreateFromFiles("Shaders/thumbnail.vert", "Shaders/thumbnail.frag");
}

void AssetBrowser::CleanupThumbnailFBO()
{
	if (thumbnailFBO) { glDeleteFramebuffers(1, &thumbnailFBO); thumbnailFBO = 0; }
	if (thumbnailTexture) { glDeleteTextures(1, &thumbnailTexture); thumbnailTexture = 0; }
	if (thumbnailDepth) { glDeleteRenderbuffers(1, &thumbnailDepth); thumbnailDepth = 0; }
}

// Empties the search box. While it is being edited ImGui keeps its own copy of the text (and would
// write the old query back): stop editing it first.
void AssetBrowser::ClearSearchBox()
{
	if (searchInputId != 0 && ImGui::GetCurrentContext() && ImGui::GetActiveID() == searchInputId) ImGui::ClearActiveID();
	searchBuffer[0] = '\0';
}

std::string AssetBrowser::TakeSceneOpenRequest()
{
	std::string r;
	r.swap(sceneOpenRequest);
	return r;
}

// =====================================================================
// Listing and navigation
// =====================================================================

void AssetBrowser::Navigate(const fs::path& dir, bool addToHistory)
{
	std::error_code ec;
	fs::path target = fs::is_directory(dir, ec) ? dir : root;
	if (addToHistory) {
		if (historyIndex >= 0 && historyIndex < (int)history.size() && history[historyIndex] == target) {
			// Already there
		} else {
			history.resize(historyIndex + 1);
			history.push_back(target);
			historyIndex = (int)history.size() - 1;
		}
	}
	currentAssetPath = target;
	selected.clear();
	anchorIndex = -1;
	ClearSearchBox();
	searchDebouncePending = false;
	if (IsSearching()) { StopSearch(); activeQuery.clear(); }
	revealInTree = true;

	// Thumbnails queued for the folder we leave are not needed first anymore
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		for (const auto& job : jobs) {
			auto it = thumbs.find(job.key);
			if (it != thumbs.end() && it->second.state == ThumbState::Queued) it->second.state = ThumbState::None;
		}
		jobs.clear();
	}
	ListCurrentDirectory();
}

void AssetBrowser::ListCurrentDirectory()
{
	currentAssets.clear();
	std::error_code ec;
	if (!fs::is_directory(currentAssetPath, ec)) currentAssetPath = root;
	currentDirStamp = Stamp(currentAssetPath);
	for (fs::directory_iterator it(currentAssetPath, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
		AssetInfo info;
		if (MakeInfo(*it, info, showSceneData)) currentAssets.push_back(std::move(info));
	}
	// Selected items that are gone
	std::set<std::string> present;
	for (auto& a : currentAssets) present.insert(a.pathStr);
	if (!IsSearching())
		for (auto it = selected.begin(); it != selected.end();)
			it = present.count(*it) ? std::next(it) : selected.erase(it);
	displayDirty = true;
}

void AssetBrowser::RefreshAssetList()
{
	treeChildren.clear();
	ListCurrentDirectory();
	if (IsSearching()) StartSearch(activeQuery);
}

const std::vector<fs::path>& AssetBrowser::SubFolders(const fs::path& dir)
{
	std::string key = Narrow(dir);
	auto it = treeChildren.find(key);
	if (it != treeChildren.end()) return it->second;
	std::vector<fs::path> subs;
	std::error_code ec;
	for (fs::directory_iterator d(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && d != end; d.increment(ec)) {
		std::error_code ec2;
		if (!d->is_directory(ec2)) continue;
		std::string name = Utf8(d->path().filename());
		if (name.empty() || name[0] == '.') continue;
		subs.push_back(d->path());
	}
	std::sort(subs.begin(), subs.end(), [](const fs::path& a, const fs::path& b) { return Lower(Utf8(a.filename())) < Lower(Utf8(b.filename())); });
	return treeChildren[key] = std::move(subs);
}

void AssetBrowser::RebuildDisplayList()
{
	const std::vector<AssetInfo>& source = IsSearching() ? searchResults : currentAssets;
	displayAssets.clear();
	for (const auto& a : source)
		if (typeFilter == 0 || a.type == AssetType::Folder || (int)a.type + 1 == typeFilter) displayAssets.push_back(a);
	if (typeFilter != 0 && IsSearching())
		displayAssets.erase(std::remove_if(displayAssets.begin(), displayAssets.end(), [](const AssetInfo& a) { return a.type == AssetType::Folder; }), displayAssets.end());

	auto key = sortKey;
	bool asc = sortAscending;
	std::stable_sort(displayAssets.begin(), displayAssets.end(), [key, asc](const AssetInfo& a, const AssetInfo& b) {
		// Folders first, whatever the order
		bool fa = a.type == AssetType::Folder, fb = b.type == AssetType::Folder;
		if (fa != fb) return fa;
		int c = 0;
		switch (key) {
		case SortKey::Type:     c = (int)a.type - (int)b.type; break;
		case SortKey::Size:     c = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0); break;
		case SortKey::Modified: c = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0); break;
		default: break;
		}
		if (c == 0) c = a.lowerName.compare(b.lowerName);
		return asc ? c < 0 : c > 0;
	});
	displayDirty = false;
}

// =====================================================================
// Search (current folder and below, on a background thread)
// =====================================================================

void AssetBrowser::StartSearch(const std::string& query)
{
	StopSearch();
	activeQuery = query;
	{
		std::lock_guard<std::mutex> lock(searchMutex);
		searchResultsShownGen = searchResultsAsyncGen; // Nothing posted before this search counts
	}
	searchResults.clear();
	displayDirty = true;
	searchCancel = false;
	searchRunning = true;
	fs::path from = currentAssetPath;
	bool showData = showSceneData;
	searchThread = std::thread([this, from, query, showData]() {
		std::vector<std::string> tokens;
		std::string lowerQuery = Lower(query), token;
		for (char c : lowerQuery) {
			if (c == ' ') { if (!token.empty()) tokens.push_back(token); token.clear(); }
			else token += c;
		}
		if (!token.empty()) tokens.push_back(token);

		std::vector<AssetInfo> found;
		std::error_code ec;
		for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
			if (searchCancel) { searchRunning = false; return; }
			AssetInfo info;
			if (!MakeInfo(*it, info, showData)) {
				std::error_code ec2;
				if (it->is_directory(ec2)) it.disable_recursion_pending(); // Hidden folders: not inside either
				continue;
			}
			bool match = true;
			for (const auto& t : tokens) if (info.lowerName.find(t) == std::string::npos) { match = false; break; }
			if (match) found.push_back(std::move(info));
			if (found.size() >= 5000) break;
		}
		std::lock_guard<std::mutex> lock(searchMutex);
		searchResultsAsync = std::move(found);
		searchResultsAsyncGen++;
		searchRunning = false;
	});
}

void AssetBrowser::StopSearch()
{
	searchCancel = true;
	if (searchThread.joinable()) searchThread.join();
	searchCancel = false;
	searchRunning = false;
}

// =====================================================================
// Thumbnails
// =====================================================================

void AssetBrowser::StartWorkers()
{
	for (int i = 0; i < WORKER_COUNT; i++) workers.emplace_back([this]() { WorkerLoop(); });
}

void AssetBrowser::WorkerLoop()
{
	while (true) {
		ThumbJob job;
		{
			std::unique_lock<std::mutex> lock(jobMutex);
			jobSignal.wait(lock, [this]() { return stopWorkers || !jobs.empty(); });
			if (stopWorkers) return;
			job = std::move(jobs.front());
			jobs.pop_front();
		}
		ThumbResult r;
		r.key = job.key;
		if (ReadCache(job.cacheFile, r.rgba, r.w, r.h, r.srcW, r.srcH)) {
			r.ok = true;
		} else if (job.type == AssetType::Texture) {
			int w = 0, h = 0, n = 0;
			unsigned char* data = stbi_load(job.key.c_str(), &w, &h, &n, 4);
			if (data) {
				Shrink(data, w, h, THUMB_SIZE, r.rgba, r.w, r.h);
				stbi_image_free(data);
				r.srcW = w;
				r.srcH = h;
				r.ok = true;
				WriteCache(job.cacheFile, r.rgba.data(), r.w, r.h, r.srcW, r.srcH);
			}
		} else {
			r.cacheMiss = true; // Models and materials are rendered on the main thread
		}
		std::lock_guard<std::mutex> lock(resultMutex);
		results.push_back(std::move(r));
	}
}

std::string AssetBrowser::CacheFileFor(const AssetInfo& asset)
{
	std::error_code ec;
	fs::path abs = fs::absolute(asset.path, ec);
	std::string key = Lower(Narrow(ec ? asset.path : abs)) + "|" + std::to_string(asset.modified) + "|" +
		std::to_string(asset.size) + "|v1|" + std::to_string(THUMB_SIZE);
	char name[32];
	snprintf(name, sizeof(name), "%016llx.thumb", (unsigned long long)Fnv1a(key));
	return Narrow(CACHE_DIR / name);
}

void AssetBrowser::RequestThumbnail(const AssetInfo& asset)
{
	if (asset.type != AssetType::Texture && asset.type != AssetType::Model && asset.type != AssetType::MaterialAsset) return;
	Thumb& thumb = thumbs[asset.pathStr];
	thumb.lastSeen = ImGui::GetTime();
	CacheName& name = cacheFiles[asset.pathStr];
	if (name.file.empty() || name.modified != asset.modified || name.size != asset.size) {
		if (!name.file.empty()) {
			// The file changed since its thumbnail was made
			if (thumb.tex) { glDeleteTextures(1, &thumb.tex); thumb.tex = 0; }
			thumb.state = ThumbState::None;
		}
		name.modified = asset.modified;
		name.size = asset.size;
		name.file = CacheFileFor(asset);
	}
	const std::string& cacheFile = name.file;
	if (thumb.state != ThumbState::None) return;
	thumb.state = ThumbState::Queued;
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		jobs.push_front({ asset.pathStr, cacheFile, asset.type }); // Latest seen first
	}
	jobSignal.notify_one();
}

GLuint AssetBrowser::UploadRGBA(const unsigned char* rgba, int w, int h)
{
	GLint prevBinding = 0, prevAlign = 4;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBinding);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prevBinding);
	return tex;
}

void AssetBrowser::UploadResults()
{
	std::vector<ThumbResult> ready;
	{
		std::lock_guard<std::mutex> lock(resultMutex);
		if (results.empty()) return;
		size_t take = std::min(results.size(), (size_t)UPLOADS_PER_FRAME);
		ready.assign(std::make_move_iterator(results.begin()), std::make_move_iterator(results.begin() + take));
		results.erase(results.begin(), results.begin() + take);
	}
	for (auto& r : ready) {
		auto it = thumbs.find(r.key);
		if (it == thumbs.end()) continue;
		Thumb& t = it->second;
		if (t.state != ThumbState::Queued) continue; // Forgotten meanwhile
		if (!r.ok) { t.state = r.cacheMiss ? ThumbState::NeedsRender : ThumbState::Failed; continue; }
		if (t.tex) glDeleteTextures(1, &t.tex);
		t.tex = UploadRGBA(r.rgba.data(), r.w, r.h);
		t.width = r.w; t.height = r.h;
		t.srcWidth = r.srcW; t.srcHeight = r.srcH;
		t.state = ThumbState::Ready;
	}
}

void AssetBrowser::EvictThumbnails()
{
	std::vector<std::pair<double, std::string>> live;
	for (auto& [key, t] : thumbs) if (t.tex) live.push_back({ t.lastSeen, key });
	if (live.size() <= MAX_THUMB_TEXTURES) return;
	std::sort(live.begin(), live.end());
	double now = ImGui::GetTime();
	size_t excess = live.size() - MAX_THUMB_TEXTURES * 5 / 6;
	for (size_t i = 0; i < live.size() && excess > 0; i++) {
		if (now - live[i].first < 2.0) break; // Still on screen
		Thumb& t = thumbs[live[i].second];
		glDeleteTextures(1, &t.tex);
		t.tex = 0;
		t.state = ThumbState::None; // Back from the disk cache when seen again
		excess--;
	}
}

void AssetBrowser::ForgetThumbnail(const std::string& key)
{
	auto it = thumbs.find(key);
	if (it == thumbs.end()) return;
	if (it->second.tex) glDeleteTextures(1, &it->second.tex);
	thumbs.erase(it);
	cacheFiles.erase(key);
}

bool AssetBrowser::ReadBackThumbnail(Thumb& thumb, const std::string& cacheFile)
{
	std::vector<unsigned char> pixels((size_t)THUMB_SIZE * THUMB_SIZE * 4), flipped(pixels.size());
	GLint prevPack = 4;
	glGetIntegerv(GL_PACK_ALIGNMENT, &prevPack);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, THUMB_SIZE, THUMB_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glPixelStorei(GL_PACK_ALIGNMENT, prevPack);
	// GL rows start at the bottom; thumbnails are stored top row first
	size_t row = (size_t)THUMB_SIZE * 4;
	for (int y = 0; y < THUMB_SIZE; y++)
		std::memcpy(&flipped[(size_t)y * row], &pixels[(size_t)(THUMB_SIZE - 1 - y) * row], row);
	WriteCache(cacheFile, flipped.data(), THUMB_SIZE, THUMB_SIZE, 0, 0);
	if (thumb.tex) glDeleteTextures(1, &thumb.tex);
	thumb.tex = UploadRGBA(flipped.data(), THUMB_SIZE, THUMB_SIZE);
	thumb.width = thumb.height = THUMB_SIZE;
	thumb.state = ThumbState::Ready;
	return true;
}

namespace {
	// Saves the GL state a thumbnail render touches, puts it back when destroyed
	struct GLStateGuard {
		GLint viewport[4], fbo, program, depthFunc;
		GLfloat clear[4];
		GLboolean cull, depth, blend, scissor, depthMask;
		GLStateGuard()
		{
			glGetIntegerv(GL_VIEWPORT, viewport);
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
			glGetIntegerv(GL_CURRENT_PROGRAM, &program);
			glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
			glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
			glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
			cull = glIsEnabled(GL_CULL_FACE);
			depth = glIsEnabled(GL_DEPTH_TEST);
			blend = glIsEnabled(GL_BLEND);
			scissor = glIsEnabled(GL_SCISSOR_TEST);
		}
		~GLStateGuard()
		{
			glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
			glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
			glUseProgram((GLuint)program);
			glDepthFunc((GLenum)depthFunc);
			glClearColor(clear[0], clear[1], clear[2], clear[3]);
			glDepthMask(depthMask);
			auto set = [](GLenum cap, GLboolean on) { if (on) glEnable(cap); else glDisable(cap); };
			set(GL_CULL_FACE, cull);
			set(GL_DEPTH_TEST, depth);
			set(GL_BLEND, blend);
			set(GL_SCISSOR_TEST, scissor);
		}
	};

	void BeginThumbnailPass(GLuint fbo, int size, float grey)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, size, size);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		glClearColor(grey, grey, grey, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
}

bool AssetBrowser::RenderModelThumbnail(const AssetInfo& asset, Thumb& thumb)
{
	if (!thumbnailFBO) { thumb.state = ThumbState::Failed; return true; }
	AssetManager* assets = ServiceLocator::GetAssetManager();
	// Loading a model just for its thumbnail: not when memory is short, not too many at once
	bool canLoad = !memoryLow && assets->GetActiveTasksCount() < 2;
	Model* model = assets->GetModel(asset.pathStr, canLoad);
	thumb.lowMemory = !model && memoryLow;
	if (!model) return false;
	if (!model->IsReady()) {
		if (model->IsFailed()) { thumb.state = ThumbState::Failed; return true; }
		return false;
	}

	GLStateGuard guard;
	BeginThumbnailPass(thumbnailFBO, THUMB_SIZE, 0.15f);
	glDisable(GL_CULL_FACE);
	thumbnailShader.UseShader();

	glm::vec3 minB = model->GetMinBound(), maxB = model->GetMaxBound();
	glm::vec3 center = (minB + maxB) * 0.5f, size = maxB - minB;
	float maxDim = std::max({ size.x, size.y, size.z });
	if (maxDim < 0.001f) maxDim = 1.0f;
	float dist = maxDim * 2.0f;
	glm::mat4 projection = glm::perspective(glm::radians(35.0f), 1.0f, dist * 0.01f, dist * 10.0f);
	glm::mat4 view = glm::lookAt(center + glm::normalize(glm::vec3(1.0f, 0.8f, 1.0f)) * dist, center, glm::vec3(0, 1, 0));
	glm::mat4 identity(1.0f);
	GLuint id = thumbnailShader.GetShaderID();
	glUniformMatrix4fv(thumbnailShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(thumbnailShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(thumbnailShader.GetModelLocation(), 1, GL_FALSE, glm::value_ptr(identity));
	glUniform1i(glGetUniformLocation(id, "theTexture"), 0);
	glUniform1i(glGetUniformLocation(id, "useMaterial"), 0);
	glUniform3f(glGetUniformLocation(id, "materialColor"), 1.0f, 1.0f, 1.0f);
	model->RenderModel(glGetUniformLocation(id, "useNormalMap"), glGetUniformLocation(id, "useDiffuseTexture"),
		glGetUniformLocation(id, "normalMap"), glGetUniformLocation(id, "theTexture"));

	return ReadBackThumbnail(thumb, cacheFiles[asset.pathStr].file);
}

bool AssetBrowser::RenderMaterialThumbnail(const AssetInfo& asset, Thumb& thumb)
{
	if (!thumbnailFBO) { thumb.state = ThumbState::Failed; return true; }
	Material* mat = Material::LoadFromFile(asset.pathStr);
	if (!mat) { thumb.state = ThumbState::Failed; return true; }
	static Mesh* sphere = nullptr;
	if (!sphere) {
		sphere = PrimitiveGenerator::CreateSphere(24, 24);
		sphere->AddRef(); // Kept for the session
	}

	GLStateGuard guard;
	BeginThumbnailPass(thumbnailFBO, THUMB_SIZE, 0.12f);
	glEnable(GL_CULL_FACE);
	thumbnailShader.UseShader();
	glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
	glm::mat4 identity(1.0f);
	GLuint id = thumbnailShader.GetShaderID();
	glUniformMatrix4fv(thumbnailShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(thumbnailShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(thumbnailShader.GetModelLocation(), 1, GL_FALSE, glm::value_ptr(identity));
	glUniform1i(glGetUniformLocation(id, "theTexture"), 0);
	glUniform1i(glGetUniformLocation(id, "useDiffuseTexture"), 0);
	glm::vec3 color = mat->GetColor();
	glUniform3f(glGetUniformLocation(id, "materialColor"), color.r, color.g, color.b);
	glUniform1f(glGetUniformLocation(id, "specularIntensity"), mat->GetSpecularIntensity());
	glUniform1f(glGetUniformLocation(id, "shininess"), mat->GetShininess());
	glUniform1i(glGetUniformLocation(id, "useMaterial"), 1);
	sphere->RenderMesh();
	delete mat;

	return ReadBackThumbnail(thumb, cacheFiles[asset.pathStr].file);
}

// =====================================================================
// Actions
// =====================================================================

void AssetBrowser::OpenAsset(const AssetInfo& asset)
{
	if (asset.type == AssetType::Folder) {
		Navigate(asset.path);
		return;
	}
	if (asset.type == AssetType::Scene) {
		sceneToConfirm = asset.pathStr;
		sceneOpenRequestPopup = true;
		return;
	}
	// Anything else: the program Windows opens it with (image viewer, code editor...)
	std::error_code ec;
	fs::path abs = fs::absolute(asset.path, ec);
	HINSTANCE r = ShellExecuteW(nullptr, L"open", (ec ? asset.path : abs).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if ((INT_PTR)r <= 32) {
		errorMessage = "No program is set to open " + asset.name + ".";
		errorOpenRequest = true;
	}
}

void AssetBrowser::RevealInExplorer(const fs::path& path)
{
	std::error_code ec;
	fs::path abs = fs::absolute(path, ec);
	std::wstring args = L"/select,\"" + (ec ? path : abs).wstring() + L"\"";
	ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void AssetBrowser::StartRename(const AssetInfo& asset)
{
	renameTarget = asset.pathStr;
	strncpy_s(renameBuffer, sizeof(renameBuffer), asset.name.c_str(), _TRUNCATE);
	renameOpenRequest = true;
}

void AssetBrowser::CreateFolder()
{
	fs::path p = currentAssetPath / "New Folder";
	std::error_code ec;
	for (int i = 1; fs::exists(p, ec); i++) p = currentAssetPath / ("New Folder " + std::to_string(i));
	if (!fs::create_directory(p, ec)) {
		errorMessage = "Could not create the folder: " + ec.message();
		errorOpenRequest = true;
		return;
	}
	treeChildren.erase(Narrow(currentAssetPath));
	ListCurrentDirectory();
	selected = { Narrow(p) };
	AssetInfo info;
	info.path = p;
	info.pathStr = Narrow(p);
	info.name = Utf8(p.filename());
	StartRename(info);
}

void AssetBrowser::CreateMaterial()
{
	fs::path p = currentAssetPath / "New Material.mat";
	std::error_code ec;
	for (int i = 1; fs::exists(p, ec); i++) p = currentAssetPath / ("New Material " + std::to_string(i) + ".mat");
	Material mat;
	if (!mat.SaveToFile(Narrow(p))) {
		errorMessage = "Could not write " + Narrow(p) + ".";
		errorOpenRequest = true;
		return;
	}
	ListCurrentDirectory();
	selected = { Narrow(p) };
	AssetInfo info;
	info.path = p;
	info.pathStr = Narrow(p);
	info.name = Utf8(p.filename());
	StartRename(info);
}

// =====================================================================
// Window
// =====================================================================

void AssetBrowser::Render(SceneManager& scene, EditorUI::WindowState& uiState)
{
	if (!uiState.isAssetBrowserOpen) return;

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float menuHeight = ImGui::GetFrameHeight();
	ImVec2 pos(uiState.leftWidth, menuHeight + (displaySize.y - menuHeight) * uiState.midHeightRatio);
	ImVec2 size(displaySize.x - uiState.leftWidth - uiState.rightWidth, (displaySize.y - menuHeight) * (1.0f - uiState.midHeightRatio));
	if (uiState.maximizedWindowID == 3) {
		pos = ImVec2(0, menuHeight);
		size = ImVec2(displaySize.x, displaySize.y - menuHeight);
	} else if (uiState.maximizedWindowID != -1) {
		return;
	}
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
	bool open = ImGui::Begin("Project", &uiState.isAssetBrowserOpen, windowFlags);
	ImGui::PopStyleVar();
	if (open) {
		uiState.CheckMaximize(3);
		windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		double now = ImGui::GetTime();
		renderedThisFrame = false;

		// Files added, removed or renamed outside the editor show up by themselves
		if (now - lastStampCheck > 1.0) {
			lastStampCheck = now;
			if (Stamp(currentAssetPath) != currentDirStamp) {
				treeChildren.erase(Narrow(currentAssetPath));
				ListCurrentDirectory();
			}
			MEMORYSTATUSEX mem{ sizeof(MEMORYSTATUSEX) };
			memoryLow = GlobalMemoryStatusEx(&mem) && mem.dwMemoryLoad >= 90;
		}

		// Finished searches
		{
			std::lock_guard<std::mutex> lock(searchMutex);
			if (searchResultsAsyncGen != searchResultsShownGen) {
				searchResultsShownGen = searchResultsAsyncGen;
				if (IsSearching()) { searchResults = searchResultsAsync; displayDirty = true; }
			}
		}
		if (searchDebouncePending && now - searchChangedAt > 0.25) {
			searchDebouncePending = false;
			StartSearch(searchBuffer);
		}

		UploadResults();
		EvictThumbnails();
		if (displayDirty) RebuildDisplayList();

		RenderToolbar();
		ImGui::Separator();

		float statusHeight = ImGui::GetFrameHeight();
		ImGui::BeginChild("##body", ImVec2(0.0f, -statusHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
		RenderFolderTree();
		ImGui::SameLine();
		if (listView) RenderList(scene);
		else RenderGrid(scene);
		ImGui::EndChild();
		RenderStatusBar();

		// Keyboard (not while typing)
		ImGuiIO& io = ImGui::GetIO();
		if (windowFocused && !io.WantTextInput) {
			if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && selected.size() == 1) {
				for (const auto& a : displayAssets) if (a.pathStr == *selected.begin()) { OpenAsset(a); break; }
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !IsSearching() && currentAssetPath != root)
				Navigate(currentAssetPath.parent_path());
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !selected.empty()) {
				deleteTargets.assign(selected.begin(), selected.end());
				deleteOpenRequest = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && selected.size() == 1) {
				for (const auto& a : displayAssets) if (a.pathStr == *selected.begin()) { StartRename(a); break; }
			}
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
				selected.clear();
				for (const auto& a : displayAssets) selected.insert(a.pathStr);
			}
			bool back = (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) || ImGui::IsMouseClicked(3);
			bool forward = (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) || ImGui::IsMouseClicked(4);
			if (back && historyIndex > 0) Navigate(history[--historyIndex], false);
			if (forward && historyIndex + 1 < (int)history.size()) Navigate(history[++historyIndex], false);
		}
		RenderPopups();
	}
	ImGui::End();
}

void AssetBrowser::RenderToolbar()
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::BeginDisabled(historyIndex <= 0);
	if (ImGui::ArrowButton("##back", ImGuiDir_Left)) Navigate(history[--historyIndex], false);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Back (Alt+Left)");
	ImGui::SameLine(0.0f, 2.0f);
	ImGui::BeginDisabled(historyIndex + 1 >= (int)history.size());
	if (ImGui::ArrowButton("##forward", ImGuiDir_Right)) Navigate(history[++historyIndex], false);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Forward (Alt+Right)");
	ImGui::SameLine(0.0f, 2.0f);
	ImGui::BeginDisabled(currentAssetPath == root);
	if (ImGui::ArrowButton("##up", ImGuiDir_Up)) Navigate(currentAssetPath.parent_path());
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Parent folder (Backspace)");
	ImGui::SameLine();
	const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

	// Right side controls, measured first so the path gets the rest of the row
	const float searchW = 190.0f, filterW = 110.0f, sliderW = 90.0f;
	float rightW = searchW + filterW + (listView ? 0.0f : sliderW) + ImGui::CalcTextSize("List").x + ImGui::CalcTextSize("+ New").x +
		ImGui::CalcTextSize("Refresh").x + style.FramePadding.x * 6.0f + style.ItemSpacing.x * 6.0f;
	float pathW = ImGui::GetContentRegionAvail().x - rightW;

	// Path: a button per folder (the start elided when it does not fit)
	{
		std::vector<fs::path> parts;
		fs::path acc;
		for (const auto& part : currentAssetPath) { acc /= part; parts.push_back(acc); }
		auto width = [&](size_t from) {
			float w = from > 0 ? ImGui::CalcTextSize("..").x + style.FramePadding.x * 2 + style.ItemSpacing.x : 0.0f;
			for (size_t i = from; i < parts.size(); i++)
				w += ImGui::CalcTextSize(Utf8(parts[i].filename()).c_str()).x + style.FramePadding.x * 2 + style.ItemSpacing.x + 10.0f;
			return w;
		};
		size_t first = 0;
		while (first + 1 < parts.size() && width(first) > pathW) first++;
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		if (first > 0) {
			ImGui::BeginDisabled();
			ImGui::Button("..");
			ImGui::EndDisabled();
			ImGui::SameLine(0.0f, 2.0f);
		}
		for (size_t i = first; i < parts.size(); i++) {
			ImGui::PushID((int)i);
			std::string label = Utf8(parts[i].filename());
			bool isLast = i + 1 == parts.size();
			if (isLast) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
			else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.72f, 0.78f, 1.0f));
			if (ImGui::Button(label.c_str()) && !isLast) Navigate(parts[i]);
			ImGui::PopStyleColor();
			if (!isLast) { ImGui::SameLine(0.0f, 0.0f); ImGui::TextDisabled(">"); ImGui::SameLine(0.0f, 0.0f); }
			ImGui::PopID();
		}
		ImGui::PopStyleColor();
	}

	ImGui::SameLine();
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rowRight - rightW));
	ImGui::SetNextItemWidth(searchW);
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
		ImGui::SetKeyboardFocusHere();
	if (ImGui::InputTextWithHint("##search", "Search here and below...", searchBuffer, sizeof(searchBuffer))) {
		if (searchBuffer[0] == '\0') {
			searchDebouncePending = false;
			StopSearch();
			activeQuery.clear();
			searchResults.clear();
			displayDirty = true;
		} else {
			searchDebouncePending = true;
			searchChangedAt = ImGui::GetTime();
		}
	}
	searchInputId = ImGui::GetItemID();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every word must appear in the name (Ctrl+F)");

	ImGui::SameLine();
	ImGui::SetNextItemWidth(filterW);
	static const char* filters[] = { "All types", "Folders", "Textures", "Models", "Materials", "Scenes", "Scripts", "Shaders", "Data", "Other" };
	if (ImGui::Combo("##filter", &typeFilter, filters, IM_ARRAYSIZE(filters))) displayDirty = true;

	ImGui::SameLine();
	if (ImGui::Button(listView ? "Grid" : "List")) listView = !listView;
	if (!listView) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(sliderW);
		ImGui::SliderFloat("##size", &cellSize, 48.0f, 192.0f, "");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Thumbnail size");
	}
	ImGui::SameLine();
	if (ImGui::Button("+ New")) ImGui::OpenPopup("##new");
	if (ImGui::BeginPopup("##new")) {
		if (ImGui::MenuItem("Folder")) CreateFolder();
		if (ImGui::MenuItem("Material")) CreateMaterial();
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh")) RefreshAssetList();
}

void AssetBrowser::RenderFolderTree()
{
	ImGui::BeginChild("##tree", ImVec2(200.0f, 0.0f), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
	RenderTreeNode(root);
	revealInTree = false;
	ImGui::EndChild();
}

void AssetBrowser::RenderTreeNode(const fs::path& dir)
{
	const auto& subs = SubFolders(dir);
	std::string key = Narrow(dir);
	bool isCurrent = Lower(key) == Lower(Narrow(currentAssetPath));
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (subs.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
	if (isCurrent) flags |= ImGuiTreeNodeFlags_Selected;
	if (dir == root) flags |= ImGuiTreeNodeFlags_DefaultOpen;
	if (revealInTree && !isCurrent && IsAncestorOrSelf(dir, currentAssetPath)) ImGui::SetNextItemOpen(true);
	if (revealInTree && isCurrent) ImGui::SetScrollHereY();
	std::string label = Utf8(dir.filename());
	bool open = ImGui::TreeNodeEx(key.c_str(), flags, "%s", label.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Navigate(dir);
	if (open) {
		// Copy: navigating from a child can invalidate the cached list
		std::vector<fs::path> children = subs;
		for (const auto& sub : children) RenderTreeNode(sub);
		ImGui::TreePop();
	}
}

// Click (Ctrl: toggle, Shift: range), double click opens
void AssetBrowser::HandleItemInteraction(int index, bool hovered, bool clicked, bool doubleClicked)
{
	(void)hovered;
	const AssetInfo& asset = displayAssets[index];
	ImGuiIO& io = ImGui::GetIO();
	if (clicked) {
		if (io.KeyShift && anchorIndex >= 0 && anchorIndex < (int)displayAssets.size()) {
			if (!io.KeyCtrl) selected.clear();
			for (int i = std::min(anchorIndex, index); i <= std::max(anchorIndex, index); i++) selected.insert(displayAssets[i].pathStr);
		} else if (io.KeyCtrl) {
			if (!selected.erase(asset.pathStr)) selected.insert(asset.pathStr);
			anchorIndex = index;
		} else {
			if (!selected.count(asset.pathStr) || selected.size() > 1) selected = { asset.pathStr };
			anchorIndex = index;
		}
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected.count(asset.pathStr)) {
		selected = { asset.pathStr };
		anchorIndex = index;
	}
	if (doubleClicked) {
		AssetInfo copy = asset; // Opening may rebuild the list
		OpenAsset(copy);
	}
}

void AssetBrowser::BeginItemDrag(const AssetInfo& asset)
{
	if (!ImGui::BeginDragDropSource()) return;
	const char* payload = "ASSET_PATH";
	if (asset.type == AssetType::MaterialAsset) payload = "MATERIAL_PATH";
	else if (asset.type == AssetType::Script) payload = "SCRIPT_PATH";
	else if (asset.type == AssetType::Scene) payload = "SCENE_PATH";
	ImGui::SetDragDropPayload(payload, asset.pathStr.c_str(), asset.pathStr.size() + 1);
	auto it = thumbs.find(asset.pathStr);
	if (it != thumbs.end() && it->second.state == ThumbState::Ready)
		ImGui::Image((ImTextureID)(intptr_t)it->second.tex, ImVec2(48, 48));
	ImGui::Text("%s", asset.name.c_str());
	const char* hint = asset.type == AssetType::Model ? "Drop in the viewport or the hierarchy" :
		asset.type == AssetType::Texture ? "Drop on a texture slot in the inspector" :
		asset.type == AssetType::MaterialAsset ? "Drop on an object" :
		asset.type == AssetType::Script ? "Drop on a script field in the inspector" : "";
	if (*hint) ImGui::TextDisabled("%s", hint);
	ImGui::EndDragDropSource();
}

void AssetBrowser::ItemTooltip(const AssetInfo& asset)
{
	if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay) || ImGui::IsMouseDragging(0)) return;
	ImGui::BeginTooltip();
	auto it = thumbs.find(asset.pathStr);
	if (it != thumbs.end() && it->second.state == ThumbState::Ready && it->second.width > 0) {
		float s = 128.0f / std::max(it->second.width, it->second.height);
		ImGui::Image((ImTextureID)(intptr_t)it->second.tex, ImVec2(it->second.width * s, it->second.height * s));
	}
	ImGui::TextUnformatted(asset.name.c_str());
	ImGui::TextDisabled("%s", TypeName(asset.type));
	if (it != thumbs.end() && it->second.srcWidth > 0) {
		ImGui::SameLine();
		ImGui::TextDisabled("%d x %d", it->second.srcWidth, it->second.srcHeight);
	}
	if (asset.type != AssetType::Folder) ImGui::TextDisabled("%s", FormatSize(asset.size).c_str());
	std::string when = FormatTime(asset.modified);
	if (!when.empty()) ImGui::TextDisabled("Modified %s", when.c_str());
	if (IsSearching()) ImGui::TextDisabled("%s", Utf8(asset.path.parent_path()).c_str());
	ImGui::EndTooltip();
}

void AssetBrowser::ItemContextMenu(const AssetInfo& asset)
{
	if (!ImGui::BeginPopupContextItem("##item")) return;
	AssetInfo a = asset; // Actions may rebuild the list
	ImGui::TextDisabled("%s", a.name.c_str());
	ImGui::Separator();
	const char* openLabel = a.type == AssetType::Folder ? "Open" : a.type == AssetType::Scene ? "Open scene" : "Open in its program";
	if (ImGui::MenuItem(openLabel, "Enter")) OpenAsset(a);
	if (IsSearching() && ImGui::MenuItem("Show in folder")) {
		Navigate(a.path.parent_path());
		selected = { a.pathStr };
	}
	if (ImGui::MenuItem("Show in Explorer")) RevealInExplorer(a.path);
	if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(a.pathStr.c_str());
	ImGui::Separator();
	if (ImGui::MenuItem("Rename", "F2", false, selected.size() <= 1)) StartRename(a);
	if (ImGui::MenuItem(selected.size() > 1 ? "Delete selected" : "Delete", "Del")) {
		deleteTargets.assign(selected.begin(), selected.end());
		if (deleteTargets.empty()) deleteTargets = { a.pathStr };
		deleteOpenRequest = true;
	}
	ImGui::EndPopup();
}

void AssetBrowser::DrawThumbnail(ImDrawList* dl, const AssetInfo& asset, ImVec2 a, ImVec2 b)
{
	std::string ext = Narrow(asset.path.extension());
	auto it = thumbs.find(asset.pathStr);
	if (it == thumbs.end() || it->second.state != ThumbState::Ready || !it->second.tex) {
		DrawTypeIcon(dl, a, b, asset.type, ext);
		if (it == thumbs.end()) return;
		Thumb& t = it->second;
		float w = b.x - a.x;
		if (t.state == ThumbState::Failed) {
			dl->AddText(ImVec2(a.x + 3, b.y - ImGui::GetFontSize() - 2), IM_COL32(240, 90, 90, 255), "failed");
		} else if (t.lowMemory) {
			dl->AddText(ImVec2(a.x + 3, b.y - ImGui::GetFontSize() - 2), IM_COL32(230, 160, 90, 255), "low memory");
		} else if (asset.type == AssetType::Model && t.state == ThumbState::NeedsRender) {
			// Loading the model for its thumbnail
			Model* model = ServiceLocator::GetAssetManager()->GetModel(asset.pathStr, false);
			float progress = model ? model->GetLoadProgress() : 0.0f;
			ImVec2 p0(a.x + w * 0.1f, b.y - 6.0f), p1(b.x - w * 0.1f, b.y - 3.0f);
			dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 48, 255), 2.0f);
			dl->AddRectFilled(p0, ImVec2(p0.x + (p1.x - p0.x) * std::clamp(progress, 0.05f, 1.0f), p1.y), IM_COL32(90, 200, 110, 255), 2.0f);
		}
		return;
	}
	Thumb& t = it->second;
	// Fit the image in the square, keeping its proportions
	float bw = b.x - a.x, bh = b.y - a.y;
	float s = std::min(bw / t.width, bh / t.height);
	ImVec2 size(t.width * s, t.height * s);
	ImVec2 p0(a.x + (bw - size.x) * 0.5f, a.y + (bh - size.y) * 0.5f), p1(p0.x + size.x, p0.y + size.y);
	if (asset.type == AssetType::Texture) {
		// Checkerboard behind transparent textures
		dl->PushClipRect(p0, p1, true);
		float cell = std::max(4.0f, bw / 12.0f);
		dl->AddRectFilled(p0, p1, IM_COL32(70, 70, 76, 255));
		for (float y = p0.y; y < p1.y; y += cell)
			for (float x = p0.x + (fmodf((y - p0.y) / cell, 2.0f) >= 1.0f ? cell : 0.0f); x < p1.x; x += cell * 2.0f)
				dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell, y + cell), IM_COL32(96, 96, 104, 255));
		dl->PopClipRect();
	}
	dl->AddImage((ImTextureID)(intptr_t)t.tex, p0, p1);
}

void AssetBrowser::RenderGrid(SceneManager& scene)
{
	(void)scene;
	ImGui::BeginChild("##grid", ImVec2(0.0f, 0.0f));
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float fontH = ImGui::GetFontSize();
	const float pad = 8.0f;
	const float nameH = fontH * 2.0f + 4.0f;
	const ImVec2 item(cellSize, cellSize + nameH);
	const float availW = ImGui::GetContentRegionAvail().x;
	const int cols = std::max(1, (int)((availW + pad) / (item.x + pad)));
	const int count = (int)displayAssets.size();
	const int rows = (count + cols - 1) / cols;
	bool anyItemClicked = false;

	if (count == 0) {
		ImGui::Dummy(ImVec2(0.0f, 8.0f));
		ImGui::TextDisabled(IsSearching() ? (searchRunning ? "Searching..." : "Nothing found here or below") : "This folder is empty");
	}

	ImGuiListClipper clipper;
	clipper.Begin(rows, item.y + pad);
	bool stop = false; // An action changed the list (navigation...): the rest waits for the next frame
	while (!stop && clipper.Step()) {
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd && !stop; row++) {
			ImVec2 rowStart = ImGui::GetCursorScreenPos();
			for (int col = 0; col < cols; col++) {
				int i = row * cols + col;
				if (i >= count) break;
				const AssetInfo& asset = displayAssets[i];
				ImVec2 p(rowStart.x + col * (item.x + pad), rowStart.y);
				ImGui::SetCursorScreenPos(p);
				ImGui::PushID(i);
				ImGui::InvisibleButton("##cell", item);
				bool hovered = ImGui::IsItemHovered();
				bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
				bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				anyItemClicked |= clicked || ImGui::IsItemClicked(ImGuiMouseButton_Right);
				HandleItemInteraction(i, hovered, clicked, doubleClicked);
				if (displayDirty) { ImGui::PopID(); stop = true; break; }
				BeginItemDrag(asset);
				ItemTooltip(asset);
				ItemContextMenu(asset);

				bool isSelected = selected.count(asset.pathStr) > 0;
				ImVec2 q(p.x + item.x, p.y + item.y);
				if (isSelected) dl->AddRectFilled(p, q, IM_COL32(62, 100, 180, 130), 5.0f);
				else if (hovered) dl->AddRectFilled(p, q, IM_COL32(255, 255, 255, 18), 5.0f);
				ImVec2 t0(p.x + 4.0f, p.y + 4.0f), t1(p.x + cellSize - 4.0f, p.y + cellSize - 4.0f);
				RequestThumbnail(asset);
				auto th = thumbs.find(asset.pathStr);
				if (th != thumbs.end() && th->second.state == ThumbState::NeedsRender && !renderedThisFrame) {
					bool done = asset.type == AssetType::Model ? RenderModelThumbnail(asset, th->second) : RenderMaterialThumbnail(asset, th->second);
					renderedThisFrame = done || renderedThisFrame;
				}
				DrawThumbnail(dl, asset, t0, t1);

				std::string l1, l2;
				SplitName(asset.name, item.x - 4.0f, l1, l2);
				ImU32 textCol = isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 215, 224, 255);
				float y = p.y + cellSize;
				float w1 = ImGui::CalcTextSize(l1.c_str()).x;
				dl->AddText(ImVec2(p.x + (item.x - w1) * 0.5f, y), textCol, l1.c_str());
				if (!l2.empty()) {
					float w2 = ImGui::CalcTextSize(l2.c_str()).x;
					dl->AddText(ImVec2(p.x + (item.x - w2) * 0.5f, y + fontH), textCol, l2.c_str());
				}
				ImGui::PopID();
			}
			ImGui::SetCursorScreenPos(rowStart);
			ImGui::Dummy(ImVec2(availW, item.y + pad));
		}
	}
	clipper.End();

	// Empty space: click clears the selection, right click offers New / Refresh
	if (ImGui::IsWindowHovered() && !anyItemClicked && !ImGui::IsAnyItemHovered()) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl) selected.clear();
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##space");
	}
	if (ImGui::BeginPopup("##space")) {
		if (ImGui::MenuItem("New folder")) CreateFolder();
		if (ImGui::MenuItem("New material")) CreateMaterial();
		ImGui::Separator();
		if (ImGui::MenuItem("Show in Explorer")) RevealInExplorer(currentAssetPath);
		if (ImGui::MenuItem("Show engine cache files (.mesh, .inst, .assbin)", nullptr, &showSceneData)) RefreshAssetList();
		if (ImGui::MenuItem("Refresh")) RefreshAssetList();
		ImGui::EndPopup();
	}
	ImGui::EndChild();
}

void AssetBrowser::RenderList(SceneManager& scene)
{
	(void)scene;
	ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
	int columns = IsSearching() ? 5 : 4;
	if (!ImGui::BeginTable("##list", columns, flags)) return;
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 3.0f, (ImGuiID)SortKey::Name);
	ImGui::TableSetupColumn("Type", 0, 1.0f, (ImGuiID)SortKey::Type);
	ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_PreferSortDescending, 0.8f, (ImGuiID)SortKey::Size);
	ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_PreferSortDescending, 1.2f, (ImGuiID)SortKey::Modified);
	if (IsSearching()) ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_NoSort, 2.0f);
	ImGui::TableHeadersRow();
	if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
		if (specs->SpecsDirty && specs->SpecsCount > 0) {
			sortKey = (SortKey)specs->Specs[0].ColumnUserID;
			sortAscending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
			specs->SpecsDirty = false;
			RebuildDisplayList();
		}
	}

	const float rowH = ImGui::GetFrameHeight();
	ImGuiListClipper clipper;
	clipper.Begin((int)displayAssets.size(), rowH);
	bool stop = false;
	while (!stop && clipper.Step()) {
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
			const AssetInfo& asset = displayAssets[i];
			ImGui::TableNextRow(0, rowH);
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(i);
			bool isSelected = selected.count(asset.pathStr) > 0;
			ImVec2 start = ImGui::GetCursorScreenPos();
			ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowH));
			bool hovered = ImGui::IsItemHovered();
			bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
			bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			HandleItemInteraction(i, hovered, clicked, doubleClicked);
			if (displayDirty) { ImGui::PopID(); stop = true; break; }
			BeginItemDrag(asset);
			ItemTooltip(asset);
			ItemContextMenu(asset);
			RequestThumbnail(asset);
			DrawThumbnail(ImGui::GetWindowDrawList(), asset, start, ImVec2(start.x + rowH, start.y + rowH));
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::SetCursorScreenPos(ImVec2(start.x + rowH + 6.0f, start.y + (rowH - ImGui::GetFontSize()) * 0.5f));
			ImGui::TextUnformatted(asset.name.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextDisabled("%s", TypeName(asset.type));
			ImGui::TableSetColumnIndex(2);
			if (asset.type != AssetType::Folder) ImGui::TextDisabled("%s", FormatSize(asset.size).c_str());
			ImGui::TableSetColumnIndex(3);
			ImGui::TextDisabled("%s", FormatTime(asset.modified).c_str());
			if (IsSearching()) {
				ImGui::TableSetColumnIndex(4);
				ImGui::TextDisabled("%s", Utf8(asset.path.parent_path()).c_str());
			}
			ImGui::PopID();
		}
	}
	clipper.End();
	ImGui::EndTable();
}

void AssetBrowser::RenderStatusBar()
{
	ImGui::AlignTextToFramePadding();
	if (IsSearching() && searchRunning)
		ImGui::TextDisabled("Searching for \"%s\"...", activeQuery.c_str());
	else if (IsSearching())
		ImGui::TextDisabled("%d found for \"%s\"", (int)displayAssets.size(), activeQuery.c_str());
	else
		ImGui::TextDisabled("%d items", (int)displayAssets.size());
	if (selected.size() == 1) {
		for (const auto& a : displayAssets) {
			if (a.pathStr != *selected.begin()) continue;
			ImGui::SameLine();
			ImGui::TextDisabled("|  %s  (%s", a.name.c_str(), TypeName(a.type));
			auto it = thumbs.find(a.pathStr);
			if (it != thumbs.end() && it->second.srcWidth > 0) { ImGui::SameLine(0, 0); ImGui::TextDisabled(", %d x %d", it->second.srcWidth, it->second.srcHeight); }
			if (a.type != AssetType::Folder) { ImGui::SameLine(0, 0); ImGui::TextDisabled(", %s", FormatSize(a.size).c_str()); }
			ImGui::SameLine(0, 0);
			ImGui::TextDisabled(")");
			break;
		}
	} else if (selected.size() > 1) {
		ImGui::SameLine();
		ImGui::TextDisabled("|  %d selected", (int)selected.size());
	}
}

void AssetBrowser::RenderPopups()
{
	if (renameOpenRequest) { ImGui::OpenPopup("Rename##asset"); renameOpenRequest = false; }
	if (deleteOpenRequest) { ImGui::OpenPopup("Delete##asset"); deleteOpenRequest = false; }
	if (sceneOpenRequestPopup) { ImGui::OpenPopup("Open scene##asset"); sceneOpenRequestPopup = false; }
	if (errorOpenRequest) { ImGui::OpenPopup("Project##error"); errorOpenRequest = false; }

	if (ImGui::BeginPopupModal("Rename##asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(320.0f);
		bool commit = ImGui::InputText("##name", renameBuffer, sizeof(renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		ImGui::TextDisabled("Scenes that use it by its old path will not find it.");
		commit |= ImGui::Button("Rename", ImVec2(100, 0));
		ImGui::SameLine();
		bool cancel = ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		if (commit) {
			std::string name = renameBuffer;
			fs::path from = renameTarget;
			if (name.empty() || name.find_first_of("<>:\"/\\|?*") != std::string::npos) {
				errorMessage = "A name cannot be empty or contain < > : \" / \\ | ? *";
				errorOpenRequest = true;
			} else {
				fs::path to = from.parent_path() / fs::u8path(name);
				std::error_code ec;
				if (to != from && fs::exists(to, ec)) {
					errorMessage = "Something named " + name + " is already there.";
					errorOpenRequest = true;
				} else if (to != from) {
					fs::rename(from, to, ec);
					if (ec) { errorMessage = "Could not rename: " + ec.message(); errorOpenRequest = true; }
					else {
						ForgetThumbnail(renameTarget);
						treeChildren.clear();
						selected = { Narrow(to) };
						if (IsSearching()) StartSearch(activeQuery);
						ListCurrentDirectory();
					}
				}
			}
			ImGui::CloseCurrentPopup();
		} else if (cancel) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("Delete##asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (deleteTargets.size() == 1) ImGui::Text("Move \"%s\" to the Recycle Bin?", Utf8(fs::path(deleteTargets[0]).filename()).c_str());
		else ImGui::Text("Move %d items to the Recycle Bin?", (int)deleteTargets.size());
		ImGui::TextDisabled("Scenes that use them will not find them (they can be restored from the Recycle Bin).");
		if (ImGui::Button("Delete", ImVec2(100, 0))) {
			std::wstring from;
			for (const auto& t : deleteTargets) {
				std::error_code ec;
				fs::path abs = fs::absolute(t, ec);
				from += (ec ? fs::path(t) : abs).wstring();
				from.push_back(L'\0');
			}
			from.push_back(L'\0');
			SHFILEOPSTRUCTW op{};
			op.wFunc = FO_DELETE;
			op.pFrom = from.c_str();
			op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
			int r = SHFileOperationW(&op);
			if (r != 0 || op.fAnyOperationsAborted) {
				errorMessage = "Some items could not be deleted (error " + std::to_string(r) + ").";
				errorOpenRequest = true;
			}
			for (const auto& t : deleteTargets) ForgetThumbnail(t);
			deleteTargets.clear();
			selected.clear();
			treeChildren.clear();
			if (IsSearching()) StartSearch(activeQuery);
			ListCurrentDirectory();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			deleteTargets.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("Open scene##asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Open %s?", Utf8(fs::path(sceneToConfirm).filename()).c_str());
		ImGui::TextDisabled("Unsaved changes to the current scene will be lost.");
		if (ImGui::Button("Open", ImVec2(100, 0))) {
			sceneOpenRequest = sceneToConfirm;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("Project##error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(errorMessage.c_str());
		if (ImGui::Button("OK", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}
