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

#include <QObject>
#include <QString>

#include <array>

/* Global overlay layers that sit above whatever scene is on program.
 *
 * The compositing needs no new rendering code: obs_view_render walks the output
 * channels in ascending order and draws each one over the last, so a source on
 * a channel above the scene is, by construction, an overlay that survives every
 * scene change. Channel 0 carries the scene transition and 1 to 6 the audio
 * devices the scene collection saves, so the overlays start past those.
 *
 * Each layer keeps an OBS transition on its channel and moves between "nothing"
 * and its assigned source through it. That is the same mechanism the program
 * transition uses, which is where Cut and Fade come from without special cases,
 * and why another transition type can be added later without touching this
 * class.
 *
 * What the transition actually shows is not the assigned source but a private
 * scene holding it, because an output channel draws a source and nothing else:
 * there is no transform on a channel, so a source parked on one can only ever
 * fill the frame. Putting it in a scene makes it a scene item, which is what
 * carries position, scale, rotation and crop, and lets the ordinary Transform
 * window edit a layer. */
class OverlayManager : public QObject {
	Q_OBJECT

public:
	/* Raising this is enough to add layers; nothing below assumes four. */
	static constexpr int kOverlayCount = 4;

	/* First free output channel: 0 is the program transition and 1 to 6 are
	 * the audio devices, the last of which is AuxAudioDevice4. Starting any
	 * lower would put a layer on a channel the scene collection saves as an
	 * audio device, which writes the layer's transition into the collection
	 * and hands the channel back to the audio device on the next load. */
	static constexpr int kFirstChannel = 7;

	static_assert(kFirstChannel + kOverlayCount <= MAX_CHANNELS,
		      "overlay layers do not fit in the output channels");

	struct Config {
		QString name;
		QString sourceUuid;
		QString transitionId = "fade_transition";
		uint32_t durationMs = 300;

		/* Where the source sits inside the layer. Held here as well as on
		 * the item so a layer whose source has gone missing does not
		 * forget its placement, and so swapping the graphic on a layer
		 * keeps it where it was put. */
		obs_transform_info transform = DefaultTransform();
		obs_sceneitem_crop crop = {};
	};

	/* Top left, unscaled: what OBS gives a source dropped into a scene. */
	static obs_transform_info DefaultTransform();

private:
	struct Layer {
		Config config;
		OBSSourceAutoRelease transition;

		/* Holds the assigned source so it has a transform. Created once
		 * and kept for the life of the layer, empty or not. */
		OBSSceneAutoRelease scene;
		OBSSceneItem item;

		bool on = false;

		obs_hotkey_id toggleHotkey = OBS_INVALID_HOTKEY_ID;
	};

	std::array<Layer, kOverlayCount> layers;
	obs_hotkey_id clearAllHotkey = OBS_INVALID_HOTKEY_ID;

	/* Builds the transition for a layer and parks it on the layer's channel.
	 * Called again when the transition type changes. */
	void RebuildTransition(int index);

	/* Puts the assigned source into the layer's scene, transformed as the
	 * config says. Called again whenever the source changes. */
	void RebuildItem(int index);

	/* Moves the item to where the config says, for a layer that keeps its
	 * source and only changes place. */
	void ApplyTransform(int index);

	void ApplyState(int index, bool immediate);

	static void ToggleHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void ClearAllHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

public:
	explicit OverlayManager(QObject *parent = nullptr);
	~OverlayManager();

	int Count() const { return kOverlayCount; }

	const Config &GetConfig(int index) const;
	void SetConfig(int index, const Config &config);

	/* The source assigned to a layer, or an empty ref when the layer is
	 * empty or its source has gone missing. */
	OBSSource GetSource(int index) const;

	/* The assigned source as it sits in the layer, for the Transform window
	 * to edit. Empty while the layer has no source. */
	OBSSceneItem GetItem(int index) const;

	/* True when the layer points at a source that no longer exists, which
	 * the panel reports rather than silently clearing. */
	bool IsSourceMissing(int index) const;

	bool IsOn(int index) const;
	void SetOn(int index, bool on);
	void Toggle(int index);

	/* Empties one layer without touching the others. */
	void Clear(int index);
	void ClearAll();

	/* Display name for buttons and tooltips: the layer's own name, else the
	 * assigned source's name. */
	QString DisplayName(int index) const;

	void RegisterHotkeys();
	void UnregisterHotkeys();

	obs_data_array_t *SaveHotkeys() const;
	void LoadHotkeys(obs_data_array_t *array);

	/* Stored with the scene collection, since the sources a layer points at
	 * belong to one. */
	obs_data_t *Save() const;
	void Load(obs_data_t *data);

	/* Drops every layer and its channel, for a scene collection change. */
	void Reset();

signals:
	void overlayChanged(int index);
};
