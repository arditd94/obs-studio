/******************************************************************************
    Copyright (C) 2026 by the OBS Studio contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "SceneLayout.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <algorithm>
#include <cmath>

/* ------------------------------------------------------------------------- */
/* LayoutSlot */

bool LayoutSlot::operator==(const LayoutSlot &other) const
{
	constexpr float epsilon = 0.0001f;

	return std::fabs(x - other.x) < epsilon && std::fabs(y - other.y) < epsilon &&
	       std::fabs(cx - other.cx) < epsilon && std::fabs(cy - other.cy) < epsilon;
}

void LayoutSlot::Normalize()
{
	cx = std::clamp(cx, kMinLayoutSlotSize, 1.0f);
	cy = std::clamp(cy, kMinLayoutSlotSize, 1.0f);
	x = std::clamp(x, 0.0f, 1.0f - cx);
	y = std::clamp(y, 0.0f, 1.0f - cy);
}

/* ------------------------------------------------------------------------- */
/* Serialization helpers */

static obs_data_t *SlotToData(const LayoutSlot &slot)
{
	obs_data_t *data = obs_data_create();

	obs_data_set_double(data, "x", slot.x);
	obs_data_set_double(data, "y", slot.y);
	obs_data_set_double(data, "cx", slot.cx);
	obs_data_set_double(data, "cy", slot.cy);

	return data;
}

static LayoutSlot SlotFromData(obs_data_t *data)
{
	LayoutSlot slot;

	slot.x = (float)obs_data_get_double(data, "x");
	slot.y = (float)obs_data_get_double(data, "y");
	slot.cx = (float)obs_data_get_double(data, "cx");
	slot.cy = (float)obs_data_get_double(data, "cy");
	slot.Normalize();

	return slot;
}

static obs_data_t *LayoutToData(const SceneLayout &layout)
{
	obs_data_t *data = obs_data_create();
	OBSDataArrayAutoRelease slotArray = obs_data_array_create();

	for (const LayoutSlot &slot : layout.slotList) {
		OBSDataAutoRelease slotData = SlotToData(slot);
		obs_data_array_push_back(slotArray, slotData);
	}

	obs_data_set_string(data, "id", layout.id.c_str());
	obs_data_set_string(data, "name", layout.name.c_str());
	obs_data_set_array(data, "slots", slotArray);

	return data;
}

/* Returns false when the payload does not describe a usable layout, so a
 * corrupt or hand-edited file cannot inject empty entries into the list. */
static bool LayoutFromData(obs_data_t *data, SceneLayout &layout)
{
	const char *id = obs_data_get_string(data, "id");
	const char *name = obs_data_get_string(data, "name");

	if (!id || !*id || !name || !*name)
		return false;

	OBSDataArrayAutoRelease slotArray = obs_data_get_array(data, "slots");
	if (!slotArray)
		return false;

	const size_t count = obs_data_array_count(slotArray);
	if (count == 0 || count > kMaxLayoutSlots)
		return false;

	layout.id = id;
	layout.name = name;
	layout.builtin = false;
	layout.slotList.clear();
	layout.slotList.reserve(count);

	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease slotData = obs_data_array_item(slotArray, i);
		layout.slotList.push_back(SlotFromData(slotData));
	}

	return true;
}

/* ------------------------------------------------------------------------- */
/* SceneLayoutManager */

SceneLayoutManager::SceneLayoutManager()
{
	AddBuiltinLayouts();
}

SceneLayoutManager &SceneLayoutManager::Instance()
{
	static SceneLayoutManager manager;
	return manager;
}

void SceneLayoutManager::AddBuiltinLayouts()
{
	auto add = [this](const char *id, const char *name, std::vector<LayoutSlot> newSlots) {
		SceneLayout layout;

		layout.id = id;
		layout.name = name;
		layout.slotList = std::move(newSlots);
		layout.builtin = true;

		layouts.push_back(std::move(layout));
	};

	add("obs.fullscreen", "Fullscreen", {{0.0f, 0.0f, 1.0f, 1.0f}});

	add("obs.sidebyside", "Side by Side", {{0.0f, 0.25f, 0.5f, 0.5f}, {0.5f, 0.25f, 0.5f, 0.5f}});

	add("obs.stacked", "Stacked", {{0.25f, 0.0f, 0.5f, 0.5f}, {0.25f, 0.5f, 0.5f, 0.5f}});

	add("obs.pip", "Picture in Picture", {{0.0f, 0.0f, 1.0f, 1.0f}, {0.68f, 0.66f, 0.30f, 0.30f}});

	add("obs.piplarge", "Picture in Picture (Large)", {{0.0f, 0.0f, 1.0f, 1.0f}, {0.55f, 0.53f, 0.43f, 0.43f}});

	add("obs.threeup", "Three Up",
	    {{0.0f, 0.17f, 0.66f, 0.66f}, {0.66f, 0.17f, 0.34f, 0.33f}, {0.66f, 0.50f, 0.34f, 0.33f}});

	add("obs.quad", "Quad",
	    {{0.0f, 0.0f, 0.5f, 0.5f}, {0.5f, 0.0f, 0.5f, 0.5f}, {0.0f, 0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f}});

	add("obs.grid6", "Six Up",
	    {{0.0f, 0.17f, 0.333f, 0.33f},
	     {0.333f, 0.17f, 0.334f, 0.33f},
	     {0.667f, 0.17f, 0.333f, 0.33f},
	     {0.0f, 0.50f, 0.333f, 0.33f},
	     {0.333f, 0.50f, 0.334f, 0.33f},
	     {0.667f, 0.50f, 0.333f, 0.33f}});

	add("obs.grid9", "Nine Up",
	    {{0.0f, 0.0f, 0.333f, 0.333f},
	     {0.333f, 0.0f, 0.334f, 0.333f},
	     {0.667f, 0.0f, 0.333f, 0.333f},
	     {0.0f, 0.333f, 0.333f, 0.334f},
	     {0.333f, 0.333f, 0.334f, 0.334f},
	     {0.667f, 0.333f, 0.333f, 0.334f},
	     {0.0f, 0.667f, 0.333f, 0.333f},
	     {0.333f, 0.667f, 0.334f, 0.333f},
	     {0.667f, 0.667f, 0.333f, 0.333f}});
}

const SceneLayout *SceneLayoutManager::Find(const std::string &id) const
{
	auto it = std::find_if(layouts.begin(), layouts.end(),
			       [&id](const SceneLayout &layout) { return layout.id == id; });

	return it == layouts.end() ? nullptr : &(*it);
}

bool SceneLayoutManager::AddOrReplace(const SceneLayout &layout)
{
	if (layout.id.empty() || layout.slotList.empty() || layout.slotList.size() > kMaxLayoutSlots)
		return false;

	auto it = std::find_if(layouts.begin(), layouts.end(),
			       [&layout](const SceneLayout &existing) { return existing.id == layout.id; });

	if (it != layouts.end()) {
		/* Built-in layouts are part of the shipped set; overwriting one
		 * would make the id mean different things on different
		 * installations and break exported layouts. */
		if (it->builtin)
			return false;

		*it = layout;
		it->builtin = false;
	} else {
		layouts.push_back(layout);
		layouts.back().builtin = false;
	}

	return true;
}

bool SceneLayoutManager::Remove(const std::string &id)
{
	auto it = std::find_if(layouts.begin(), layouts.end(),
			       [&id](const SceneLayout &layout) { return layout.id == id; });

	if (it == layouts.end() || it->builtin)
		return false;

	layouts.erase(it);
	return true;
}

std::string SceneLayoutManager::GenerateId(const std::string &name) const
{
	std::string base = "user.";

	for (char c : name) {
		if (isalnum((unsigned char)c))
			base += (char)tolower((unsigned char)c);
		else if (!base.empty() && base.back() != '-')
			base += '-';
	}

	if (base == "user.")
		base += "layout";

	std::string id = base;
	int suffix = 2;

	while (Find(id))
		id = base + "-" + std::to_string(suffix++);

	return id;
}

bool SceneLayoutManager::Load()
{
	char path[512];

	if (GetAppConfigPath(path, sizeof(path), "obs-studio/layouts.json") <= 0)
		return false;

	OBSDataAutoRelease root = obs_data_create_from_json_file_safe(path, "bak");
	if (!root)
		return false;

	OBSDataArrayAutoRelease array = obs_data_get_array(root, "layouts");
	if (!array)
		return false;

	/* Drop previously loaded user layouts but keep the built-in set. */
	layouts.erase(std::remove_if(layouts.begin(), layouts.end(),
				     [](const SceneLayout &layout) { return !layout.builtin; }),
		      layouts.end());

	const size_t count = obs_data_array_count(array);

	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(array, i);
		SceneLayout layout;

		if (LayoutFromData(item, layout))
			AddOrReplace(layout);
	}

	return true;
}

bool SceneLayoutManager::Save() const
{
	char path[512];

	if (GetAppConfigPath(path, sizeof(path), "obs-studio/layouts.json") <= 0)
		return false;

	OBSDataAutoRelease root = obs_data_create();
	OBSDataArrayAutoRelease array = obs_data_array_create();

	for (const SceneLayout &layout : layouts) {
		if (layout.builtin)
			continue;

		OBSDataAutoRelease item = LayoutToData(layout);
		obs_data_array_push_back(array, item);
	}

	obs_data_set_int(root, "version", 1);
	obs_data_set_array(root, "layouts", array);

	return obs_data_save_json_safe(root, path, "tmp", "bak");
}

bool SceneLayoutManager::ImportFile(const QString &path, QString &error)
{
	OBSDataAutoRelease root = obs_data_create_from_json_file(QT_TO_UTF8(path));

	if (!root) {
		error = QTStr("Basic.Layouts.Import.ReadFailed");
		return false;
	}

	/* Accept both a bare layout and a collection, so a file produced by
	 * Export and a hand-assembled set both import cleanly. */
	OBSDataArrayAutoRelease array = obs_data_get_array(root, "layouts");
	std::vector<SceneLayout> imported;

	if (array) {
		const size_t count = obs_data_array_count(array);

		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease item = obs_data_array_item(array, i);
			SceneLayout layout;

			if (LayoutFromData(item, layout))
				imported.push_back(std::move(layout));
		}
	} else {
		SceneLayout layout;

		if (LayoutFromData(root, layout))
			imported.push_back(std::move(layout));
	}

	if (imported.empty()) {
		error = QTStr("Basic.Layouts.Import.NoLayouts");
		return false;
	}

	for (SceneLayout &layout : imported) {
		/* Never let an import silently overwrite an existing layout;
		 * give the incoming one a fresh id instead. */
		if (Find(layout.id))
			layout.id = GenerateId(layout.name);

		AddOrReplace(layout);
	}

	return Save();
}

bool SceneLayoutManager::ExportFile(const QString &path, const SceneLayout &layout, QString &error) const
{
	OBSDataAutoRelease root = obs_data_create();
	OBSDataArrayAutoRelease array = obs_data_array_create();
	OBSDataAutoRelease item = LayoutToData(layout);

	obs_data_array_push_back(array, item);
	obs_data_set_int(root, "version", 1);
	obs_data_set_array(root, "layouts", array);

	if (!obs_data_save_json_safe(root, QT_TO_UTF8(path), "tmp", nullptr)) {
		error = QTStr("Basic.Layouts.Export.WriteFailed");
		return false;
	}

	return true;
}

QString GetLayoutDisplayName(const SceneLayout &layout)
{
	if (!layout.builtin)
		return QString::fromStdString(layout.name);

	/* Built-in names are translated; the stored English name is the
	 * fallback when a translation is missing. */
	const std::string key = "Basic.Layouts.Builtin." + layout.id.substr(layout.id.find('.') + 1);
	const char *translated = nullptr;

	if (App()->TranslateString(key.c_str(), &translated) && translated)
		return QString::fromUtf8(translated);

	return QString::fromStdString(layout.name);
}

/* ------------------------------------------------------------------------- */
/* Transform application */

bool GetSceneCanvasSize(obs_scene_t *scene, uint32_t &cx, uint32_t &cy)
{
	struct obs_video_info ovi;

	obs_source_t *source = obs_scene_get_source(scene);

	if (source) {
		OBSCanvasAutoRelease canvas = obs_source_get_canvas(source);

		if (canvas && obs_canvas_get_video_info(canvas, &ovi)) {
			cx = ovi.base_width;
			cy = ovi.base_height;
			return cx > 0 && cy > 0;
		}
	}

	if (!obs_get_video_info(&ovi))
		return false;

	cx = ovi.base_width;
	cy = ovi.base_height;

	return cx > 0 && cy > 0;
}

void GetLayoutSlotTransform(const LayoutSlot &slot, uint32_t canvasCX, uint32_t canvasCY,
			    struct obs_transform_info &info)
{
	memset(&info, 0, sizeof(info));

	info.pos.x = slot.x * (float)canvasCX;
	info.pos.y = slot.y * (float)canvasCY;
	info.rot = 0.0f;
	info.scale.x = 1.0f;
	info.scale.y = 1.0f;
	info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;

	/* SCALE_INNER fits the source inside the slot without distorting it,
	 * which is what a layout is expected to do with cameras of differing
	 * aspect ratios. */
	info.bounds_type = OBS_BOUNDS_SCALE_INNER;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.bounds.x = slot.cx * (float)canvasCX;
	info.bounds.y = slot.cy * (float)canvasCY;
	info.crop_to_bounds = false;
}

bool ApplyLayoutSlot(obs_sceneitem_t *item, const LayoutSlot &slot, uint32_t canvasCX, uint32_t canvasCY)
{
	if (!item || obs_sceneitem_locked(item))
		return false;

	struct obs_transform_info info;
	GetLayoutSlotTransform(slot, canvasCX, canvasCY, info);

	obs_sceneitem_set_info2(item, &info);

	return true;
}

/* ------------------------------------------------------------------------- */
/* Undo support */

std::string SaveSceneTransforms(obs_scene_t *scene)
{
	OBSDataAutoRelease root = obs_data_create();
	OBSDataArrayAutoRelease array = obs_data_array_create();

	/* Items are enumerated bottom to top, so the running index doubles as
	 * the order position needed to restore stacking. */
	struct EnumState {
		obs_data_array_t *items;
		int order;
	};

	EnumState state = {array.Get(), 0};

	auto enumItem = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		EnumState *state = static_cast<EnumState *>(param);
		obs_data_array_t *items = state->items;

		struct obs_transform_info info;
		obs_sceneitem_get_info2(item, &info);

		OBSDataAutoRelease itemData = obs_data_create();
		obs_data_set_int(itemData, "id", obs_sceneitem_get_id(item));
		obs_data_set_int(itemData, "order", state->order++);
		obs_data_set_double(itemData, "pos_x", info.pos.x);
		obs_data_set_double(itemData, "pos_y", info.pos.y);
		obs_data_set_double(itemData, "rot", info.rot);
		obs_data_set_double(itemData, "scale_x", info.scale.x);
		obs_data_set_double(itemData, "scale_y", info.scale.y);
		obs_data_set_int(itemData, "alignment", info.alignment);
		obs_data_set_int(itemData, "bounds_type", info.bounds_type);
		obs_data_set_int(itemData, "bounds_alignment", info.bounds_alignment);
		obs_data_set_double(itemData, "bounds_x", info.bounds.x);
		obs_data_set_double(itemData, "bounds_y", info.bounds.y);
		obs_data_set_bool(itemData, "crop_to_bounds", info.crop_to_bounds);

		obs_data_array_push_back(items, itemData);

		return true;
	};

	obs_scene_enum_items(scene, enumItem, &state);
	obs_data_set_array(root, "items", array);

	/* Recorded so an undo entry can find its way back to the right scene
	 * without holding a reference to it. */
	obs_source_t *source = obs_scene_get_source(scene);
	if (source)
		obs_data_set_string(root, "scene_uuid", obs_source_get_uuid(source));

	return obs_data_get_json(root);
}

void LoadSceneTransforms(obs_scene_t *scene, const std::string &data)
{
	OBSDataAutoRelease root = obs_data_create_from_json(data.c_str());
	if (!root)
		return;

	OBSDataArrayAutoRelease array = obs_data_get_array(root, "items");
	if (!array)
		return;

	const size_t count = obs_data_array_count(array);

	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease itemData = obs_data_array_item(array, i);
		const int64_t id = obs_data_get_int(itemData, "id");

		obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, id);
		if (!item)
			continue;

		struct obs_transform_info info;
		memset(&info, 0, sizeof(info));

		info.pos.x = (float)obs_data_get_double(itemData, "pos_x");
		info.pos.y = (float)obs_data_get_double(itemData, "pos_y");
		info.rot = (float)obs_data_get_double(itemData, "rot");
		info.scale.x = (float)obs_data_get_double(itemData, "scale_x");
		info.scale.y = (float)obs_data_get_double(itemData, "scale_y");
		info.alignment = (uint32_t)obs_data_get_int(itemData, "alignment");
		info.bounds_type = (enum obs_bounds_type)obs_data_get_int(itemData, "bounds_type");
		info.bounds_alignment = (uint32_t)obs_data_get_int(itemData, "bounds_alignment");
		info.bounds.x = (float)obs_data_get_double(itemData, "bounds_x");
		info.bounds.y = (float)obs_data_get_double(itemData, "bounds_y");
		info.crop_to_bounds = obs_data_get_bool(itemData, "crop_to_bounds");

		obs_sceneitem_set_info2(item, &info);
	}

	/* Restore stacking after every transform is back in place. Applying the
	 * saved positions in ascending order rebuilds the list bottom-up, so
	 * each move lands where it was captured. */
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease itemData = obs_data_array_item(array, i);

		if (!obs_data_has_user_value(itemData, "order"))
			continue;

		obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, obs_data_get_int(itemData, "id"));

		if (item)
			obs_sceneitem_set_order_position(item, (int)obs_data_get_int(itemData, "order"));
	}
}

void RestoreSceneTransforms(const std::string &data)
{
	OBSDataAutoRelease root = obs_data_create_from_json(data.c_str());
	if (!root)
		return;

	const char *uuid = obs_data_get_string(root, "scene_uuid");
	if (!uuid || !*uuid)
		return;

	OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid);
	if (!source)
		return;

	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene)
		LoadSceneTransforms(scene, data);
}
