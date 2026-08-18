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

#include "OverlayManager.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

obs_transform_info OverlayManager::DefaultTransform()
{
	obs_transform_info info = {};

	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	vec2_set(&info.bounds, 0.0f, 0.0f);
	info.rot = 0.0f;
	info.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
	info.bounds_type = OBS_BOUNDS_NONE;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.crop_to_bounds = false;

	return info;
}

OverlayManager::OverlayManager(QObject *parent) : QObject(parent)
{
	for (int i = 0; i < kOverlayCount; i++) {
		const QString name = QString("obs_overlay_%1_scene").arg(i + 1);

		layers[i].scene = obs_scene_create_private(QT_TO_UTF8(name));

		RebuildTransition(i);
	}
}

OverlayManager::~OverlayManager()
{
	UnregisterHotkeys();

	for (int i = 0; i < kOverlayCount; i++) {
		obs_set_output_source(kFirstChannel + i, nullptr);
	}
}

void OverlayManager::RebuildTransition(int index)
{
	Layer &layer = layers[index];

	const QString id = layer.config.transitionId.isEmpty() ? QString("fade_transition") : layer.config.transitionId;
	const QString name = QString("obs_overlay_%1").arg(index + 1);

	OBSSourceAutoRelease transition = obs_source_create_private(QT_TO_UTF8(id), QT_TO_UTF8(name), nullptr);

	if (!transition) {
		return;
	}

	struct obs_video_info ovi;

	if (obs_get_video_info(&ovi)) {
		obs_transition_set_size(transition, ovi.base_width, ovi.base_height);
	}

	obs_transition_set_alignment(transition, OBS_ALIGN_CENTER);
	obs_transition_set_scale_type(transition, OBS_TRANSITION_SCALE_ASPECT);

	layer.transition = std::move(transition);

	/* The transition lives on the channel whether the layer is up or not,
	 * so a fade out has something to draw while it runs. */
	obs_set_output_source(kFirstChannel + index, layer.transition);

	ApplyState(index, true);
}

void OverlayManager::RebuildItem(int index)
{
	Layer &layer = layers[index];

	if (!layer.scene) {
		return;
	}

	if (layer.item) {
		obs_sceneitem_remove(layer.item);
		layer.item = nullptr;
	}

	OBSSource source = GetSource(index);

	if (!source) {
		return;
	}

	layer.item = obs_scene_add(layer.scene, source);

	ApplyTransform(index);
}

void OverlayManager::ApplyTransform(int index)
{
	Layer &layer = layers[index];

	if (!layer.item) {
		return;
	}

	obs_sceneitem_defer_update_begin(layer.item);
	obs_sceneitem_set_info2(layer.item, &layer.config.transform);
	obs_sceneitem_set_crop(layer.item, &layer.config.crop);
	obs_sceneitem_defer_update_end(layer.item);
}

void OverlayManager::ApplyState(int index, bool immediate)
{
	Layer &layer = layers[index];

	if (!layer.transition) {
		return;
	}

	/* The scene rather than the source: it is what carries the transform,
	 * and it is only worth showing once something is in it. */
	OBSSource target = (layer.on && layer.item) ? OBSSource(obs_scene_get_source(layer.scene)) : OBSSource();

	/* Cut is a fade of no length rather than a separate path. */
	const uint32_t duration = immediate ? 0 : layer.config.durationMs;

	if (duration == 0) {
		obs_transition_set(layer.transition, target);
	} else {
		obs_transition_start(layer.transition, OBS_TRANSITION_MODE_AUTO, duration, target);
	}
}

const OverlayManager::Config &OverlayManager::GetConfig(int index) const
{
	return layers[index].config;
}

void OverlayManager::SetConfig(int index, const Config &config)
{
	Layer &layer = layers[index];
	const bool transitionChanged = layer.config.transitionId != config.transitionId;
	const bool sourceChanged = layer.config.sourceUuid != config.sourceUuid;

	layer.config = config;

	if (sourceChanged) {
		RebuildItem(index);
	} else {
		/* Saving the same graphic from a new spot in the preview changes
		 * only where it sits, which the item takes without being torn
		 * down and put back. */
		ApplyTransform(index);
	}

	if (transitionChanged) {
		RebuildTransition(index);
	} else {
		/* A source swap on a layer that is up should be visible at once
		 * rather than at the next toggle. */
		ApplyState(index, false);
	}

	emit overlayChanged(index);
}

OBSSource OverlayManager::GetSource(int index) const
{
	const QString &uuid = layers[index].config.sourceUuid;

	if (uuid.isEmpty()) {
		return OBSSource();
	}

	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(uuid));

	return OBSSource(source.Get());
}

OBSSceneItem OverlayManager::GetItem(int index) const
{
	return layers[index].item;
}

bool OverlayManager::IsSourceMissing(int index) const
{
	return !layers[index].config.sourceUuid.isEmpty() && !GetSource(index);
}

bool OverlayManager::IsOn(int index) const
{
	return layers[index].on;
}

void OverlayManager::SetOn(int index, bool on)
{
	Layer &layer = layers[index];

	/* An empty layer has nothing to raise, so it stays down instead of
	 * reporting a state the output cannot show. */
	if (on && !layer.item) {
		return;
	}

	if (layer.on == on) {
		return;
	}

	layer.on = on;
	ApplyState(index, false);

	emit overlayChanged(index);
}

void OverlayManager::Toggle(int index)
{
	SetOn(index, !layers[index].on);
}

void OverlayManager::Clear(int index)
{
	Layer &layer = layers[index];

	layer.on = false;
	layer.config.sourceUuid.clear();
	layer.config.name.clear();

	/* The placement goes with the source rather than outliving it: an empty
	 * layer should hand the next graphic a clean slate. */
	layer.config.transform = DefaultTransform();
	layer.config.crop = {};

	RebuildItem(index);
	ApplyState(index, false);

	emit overlayChanged(index);
}

void OverlayManager::ClearAll()
{
	for (int i = 0; i < kOverlayCount; i++) {
		Clear(i);
	}
}

QString OverlayManager::DisplayName(int index) const
{
	const Config &config = layers[index].config;

	if (!config.name.isEmpty()) {
		return config.name;
	}

	OBSSource source = GetSource(index);

	if (source) {
		return QString::fromUtf8(obs_source_get_name(source));
	}

	return QString();
}

void OverlayManager::ToggleHotkeyPressed(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed) {
		return;
	}

	OverlayManager *manager = static_cast<OverlayManager *>(data);

	for (int i = 0; i < kOverlayCount; i++) {
		if (manager->layers[i].toggleHotkey == id) {
			QMetaObject::invokeMethod(manager, [manager, i]() { manager->Toggle(i); });
			return;
		}
	}
}

void OverlayManager::ClearAllHotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed) {
		return;
	}

	OverlayManager *manager = static_cast<OverlayManager *>(data);

	QMetaObject::invokeMethod(manager, [manager]() { manager->ClearAll(); });
}

void OverlayManager::RegisterHotkeys()
{
	UnregisterHotkeys();

	for (int i = 0; i < kOverlayCount; i++) {
		const QString name = QString("OBSBasic.Overlay%1").arg(i + 1);
		const QString description = QTStr("Basic.Overlay.Hotkey").arg(QString::number(i + 1));

		layers[i].toggleHotkey = obs_hotkey_register_frontend(QT_TO_UTF8(name), QT_TO_UTF8(description),
								      OverlayManager::ToggleHotkeyPressed, this);
	}

	clearAllHotkey = obs_hotkey_register_frontend("OBSBasic.OverlayClearAll",
						      QT_TO_UTF8(QTStr("Basic.Overlay.ClearAll")),
						      OverlayManager::ClearAllHotkeyPressed, this);
}

void OverlayManager::UnregisterHotkeys()
{
	for (int i = 0; i < kOverlayCount; i++) {
		if (layers[i].toggleHotkey != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_unregister(layers[i].toggleHotkey);
			layers[i].toggleHotkey = OBS_INVALID_HOTKEY_ID;
		}
	}

	if (clearAllHotkey != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(clearAllHotkey);
		clearAllHotkey = OBS_INVALID_HOTKEY_ID;
	}
}

obs_data_array_t *OverlayManager::SaveHotkeys() const
{
	obs_data_array_t *array = obs_data_array_create();

	auto store = [array](const char *key, obs_hotkey_id id) {
		if (id == OBS_INVALID_HOTKEY_ID) {
			return;
		}

		OBSDataArrayAutoRelease bindings = obs_hotkey_save(id);
		OBSDataAutoRelease item = obs_data_create();

		obs_data_set_string(item, "key", key);
		obs_data_set_array(item, "bindings", bindings);
		obs_data_array_push_back(array, item);
	};

	for (int i = 0; i < kOverlayCount; i++) {
		const QString key = QString("overlay%1").arg(i + 1);
		store(QT_TO_UTF8(key), layers[i].toggleHotkey);
	}

	store("clear_all", clearAllHotkey);

	return array;
}

void OverlayManager::LoadHotkeys(obs_data_array_t *array)
{
	if (!array) {
		return;
	}

	const size_t count = obs_data_array_count(array);

	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(array, i);
		const QString key = QString::fromUtf8(obs_data_get_string(item, "key"));
		OBSDataArrayAutoRelease bindings = obs_data_get_array(item, "bindings");

		if (key == "clear_all") {
			if (clearAllHotkey != OBS_INVALID_HOTKEY_ID) {
				obs_hotkey_load(clearAllHotkey, bindings);
			}

			continue;
		}

		for (int layer = 0; layer < kOverlayCount; layer++) {
			if (key != QString("overlay%1").arg(layer + 1)) {
				continue;
			}

			if (layers[layer].toggleHotkey != OBS_INVALID_HOTKEY_ID) {
				obs_hotkey_load(layers[layer].toggleHotkey, bindings);
			}
		}
	}
}

namespace {

void SaveTransform(obs_data_t *item, const obs_transform_info &info, const obs_sceneitem_crop &crop)
{
	OBSDataAutoRelease data = obs_data_create();

	obs_data_set_double(data, "pos_x", info.pos.x);
	obs_data_set_double(data, "pos_y", info.pos.y);
	obs_data_set_double(data, "rot", info.rot);
	obs_data_set_double(data, "scale_x", info.scale.x);
	obs_data_set_double(data, "scale_y", info.scale.y);
	obs_data_set_int(data, "alignment", info.alignment);
	obs_data_set_int(data, "bounds_type", info.bounds_type);
	obs_data_set_int(data, "bounds_alignment", info.bounds_alignment);
	obs_data_set_double(data, "bounds_x", info.bounds.x);
	obs_data_set_double(data, "bounds_y", info.bounds.y);
	obs_data_set_bool(data, "crop_to_bounds", info.crop_to_bounds);
	obs_data_set_int(data, "crop_left", crop.left);
	obs_data_set_int(data, "crop_top", crop.top);
	obs_data_set_int(data, "crop_right", crop.right);
	obs_data_set_int(data, "crop_bottom", crop.bottom);

	obs_data_set_obj(item, "transform", data);
}

void LoadTransform(obs_data_t *item, obs_transform_info &info, obs_sceneitem_crop &crop)
{
	OBSDataAutoRelease data = obs_data_get_obj(item, "transform");

	/* A layer saved before layers had a transform keeps the default rather
	 * than collapsing to a zero scale. */
	if (!data) {
		return;
	}

	vec2_set(&info.pos, (float)obs_data_get_double(data, "pos_x"), (float)obs_data_get_double(data, "pos_y"));
	vec2_set(&info.scale, (float)obs_data_get_double(data, "scale_x"), (float)obs_data_get_double(data, "scale_y"));
	vec2_set(&info.bounds, (float)obs_data_get_double(data, "bounds_x"),
		 (float)obs_data_get_double(data, "bounds_y"));

	info.rot = (float)obs_data_get_double(data, "rot");
	info.alignment = (uint32_t)obs_data_get_int(data, "alignment");
	info.bounds_type = (enum obs_bounds_type)obs_data_get_int(data, "bounds_type");
	info.bounds_alignment = (uint32_t)obs_data_get_int(data, "bounds_alignment");
	info.crop_to_bounds = obs_data_get_bool(data, "crop_to_bounds");

	crop.left = (int)obs_data_get_int(data, "crop_left");
	crop.top = (int)obs_data_get_int(data, "crop_top");
	crop.right = (int)obs_data_get_int(data, "crop_right");
	crop.bottom = (int)obs_data_get_int(data, "crop_bottom");
}

} // namespace

obs_data_t *OverlayManager::Save() const
{
	obs_data_t *data = obs_data_create();
	OBSDataArrayAutoRelease array = obs_data_array_create();

	for (int i = 0; i < kOverlayCount; i++) {
		const Config &config = layers[i].config;
		OBSDataAutoRelease item = obs_data_create();

		obs_data_set_string(item, "name", QT_TO_UTF8(config.name));
		obs_data_set_string(item, "source", QT_TO_UTF8(config.sourceUuid));
		obs_data_set_string(item, "transition", QT_TO_UTF8(config.transitionId));
		obs_data_set_int(item, "duration", config.durationMs);
		obs_data_set_bool(item, "on", layers[i].on);

		/* Read back off the item, since the Transform window edits that
		 * directly and the config only catches up here. */
		obs_transform_info info = config.transform;
		obs_sceneitem_crop crop = config.crop;

		if (layers[i].item) {
			obs_sceneitem_get_info2(layers[i].item, &info);
			obs_sceneitem_get_crop(layers[i].item, &crop);
		}

		SaveTransform(item, info, crop);

		obs_data_array_push_back(array, item);
	}

	obs_data_set_array(data, "layers", array);

	return data;
}

void OverlayManager::Load(obs_data_t *data)
{
	Reset();

	if (!data) {
		return;
	}

	OBSDataArrayAutoRelease array = obs_data_get_array(data, "layers");

	if (!array) {
		return;
	}

	const size_t count = std::min<size_t>(obs_data_array_count(array), kOverlayCount);

	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(array, i);
		Layer &layer = layers[i];

		layer.config.name = QString::fromUtf8(obs_data_get_string(item, "name"));
		layer.config.sourceUuid = QString::fromUtf8(obs_data_get_string(item, "source"));
		layer.config.transitionId = QString::fromUtf8(obs_data_get_string(item, "transition"));
		layer.config.durationMs = (uint32_t)obs_data_get_int(item, "duration");

		if (layer.config.transitionId.isEmpty()) {
			layer.config.transitionId = "fade_transition";
		}

		LoadTransform(item, layer.config.transform, layer.config.crop);

		RebuildItem((int)i);

		/* A layer that was up is restored up, unless its source went
		 * missing while OBS was closed. */
		layer.on = obs_data_get_bool(item, "on") && layer.item != nullptr;

		RebuildTransition((int)i);

		emit overlayChanged((int)i);
	}
}

void OverlayManager::Reset()
{
	for (int i = 0; i < kOverlayCount; i++) {
		layers[i].on = false;
		layers[i].config = Config();

		/* The sources being dropped belong to the collection on its way
		 * out, so the layer lets go of its own before they go. */
		RebuildItem(i);

		/* Changing scene collection empties every output channel, so the
		 * transition is parked again rather than assumed to still sit
		 * there. */
		obs_set_output_source(kFirstChannel + i, layers[i].transition);

		ApplyState(i, true);

		emit overlayChanged(i);
	}
}
