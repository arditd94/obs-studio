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

#pragma once

#include <obs.hpp>

#include <QString>

#include <string>
#include <vector>

/* A single slot of a layout, stored in normalized canvas coordinates in the
 * 0.0 - 1.0 range. Keeping slots resolution independent means the same layout
 * applies unchanged to a 1080p and a 4K canvas, and keeps exported layouts
 * portable between installations. */
struct LayoutSlot {
	float x = 0.0f;
	float y = 0.0f;
	float cx = 1.0f;
	float cy = 1.0f;

	bool operator==(const LayoutSlot &other) const;

	/* Clamps the slot into the canvas and enforces a minimum size so a slot
	 * can never become impossible to grab in the editor. */
	void Normalize();
};

struct SceneLayout {
	std::string id;
	std::string name;
	/* Named slotList rather than slots because Qt reserves `slots` as a
	 * keyword macro. */
	std::vector<LayoutSlot> slotList;

	/* Built-in layouts ship with OBS and cannot be edited or deleted; the
	 * editor duplicates them instead. */
	bool builtin = false;
};

/* Maximum number of slots a single layout may define. Mirrors the number of
 * layers vMix exposes per input, and keeps the editor UI bounded. */
constexpr size_t kMaxLayoutSlots = 10;

/* Smallest allowed slot edge, as a fraction of the canvas. */
constexpr float kMinLayoutSlotSize = 0.05f;

class SceneLayoutManager {
	std::vector<SceneLayout> layouts;

	SceneLayoutManager();

	void AddBuiltinLayouts();

public:
	static SceneLayoutManager &Instance();

	const std::vector<SceneLayout> &Layouts() const { return layouts; }

	const SceneLayout *Find(const std::string &id) const;

	/* Inserts the layout, replacing any existing user layout with the same
	 * id. Built-in layouts are never replaced. Returns false if the layout
	 * is invalid or would collide with a built-in id. */
	bool AddOrReplace(const SceneLayout &layout);

	bool Remove(const std::string &id);

	/* Generates an id that is not currently in use. */
	std::string GenerateId(const std::string &name) const;

	/* User layouts are persisted next to the other frontend configuration,
	 * in obs-studio/layouts.json. Built-in layouts are never written out. */
	bool Load();
	bool Save() const;

	bool ImportFile(const QString &path, QString &error);
	bool ExportFile(const QString &path, const SceneLayout &layout, QString &error) const;
};

/* Built-in layout names are looked up in the locale files; user layouts use the
 * name the user typed. */
QString GetLayoutDisplayName(const SceneLayout &layout);

/* Resolves the canvas dimensions the scene is rendered on. Falls back to the
 * main video output when the scene is not attached to an explicit canvas. */
bool GetSceneCanvasSize(obs_scene_t *scene, uint32_t &cx, uint32_t &cy);

/* Translates a normalized slot into a scene item transform. The item is fitted
 * inside the slot with OBS_BOUNDS_SCALE_INNER, which preserves aspect ratio and
 * letterboxes rather than distorting the source. */
void GetLayoutSlotTransform(const LayoutSlot &slot, uint32_t canvasCX, uint32_t canvasCY,
			    struct obs_transform_info &info);

/* Applies a slot to a single scene item. Returns false when the item is locked,
 * since a layout must never silently override an explicit user lock. */
bool ApplyLayoutSlot(obs_sceneitem_t *item, const LayoutSlot &slot, uint32_t canvasCX, uint32_t canvasCY);

/* Serializes every transform in the scene so a layout application can be undone
 * as a single step. The returned string is JSON produced by obs_data. */
std::string SaveSceneTransforms(obs_scene_t *scene);

/* Restores transforms previously captured by SaveSceneTransforms. Items that no
 * longer exist are skipped. */
void LoadSceneTransforms(obs_scene_t *scene, const std::string &data);

/* Same, but resolves the scene from the uuid stored in the payload. Used by the
 * undo stack, which only carries a string. */
void RestoreSceneTransforms(const std::string &data);
